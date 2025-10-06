/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <Poco/Pipe.h>
#include <Poco/PipeStream.h>
#include <Poco/Process.h>
#include <Poco/StreamCopier.h>

#include <common/pbconvert/common.hpp>
#include <common/pbconvert/sm.hpp>
#include <common/utils/exception.hpp>
#include <common/utils/grpchelper.hpp>
#include <sm/logger/logmodule.hpp>

#include "smclient.hpp"

namespace aos::sm::smclient {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error SMClient::Init(const Config& config, common::iamclient::TLSCredentialsItf& tlsCredentials,
    iam::nodeinfoprovider::NodeInfoProviderItf& nodeInfoProvider,
    sm::resourcemanager::ResourceManagerItf& resourceManager, sm::networkmanager::NetworkManagerItf& networkManager,
    sm::logprovider::LogProviderItf& logProvider, monitoring::ResourceMonitorItf& resourceMonitor,
    sm::launcher::LauncherItf& launcher, bool secureConnection)
{
    LOG_DBG() << "Init SM client";

    mConfig           = config;
    mTLSCredentials   = &tlsCredentials;
    mNodeInfoProvider = &nodeInfoProvider;
    mResourceManager  = &resourceManager;
    mNetworkManager   = &networkManager;
    mLogProvider      = &logProvider;
    mResourceMonitor  = &resourceMonitor;
    mLauncher         = &launcher;
    mSecureConnection = secureConnection;

    return ErrorEnum::eNone;
}

Error SMClient::Start()
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Start SM client";

    if (auto err = mNodeInfoProvider->GetNodeInfo(mNodeInfo); !err.IsNone()) {
        return AOS_ERROR_WRAP(Error(err, "can't get node info"));
    }

    if (!mStopped) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "client already started"));
    }

    mStopped = false;

    mCertChangedThreadPool.Run();

    if (mSecureConnection) {
        auto [creds, err] = mTLSCredentials->GetMTLSClientCredentials(mConfig.mCertStorage.c_str());
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(Error(err, "can't get client credentials"));
        }

        mCredentialList.push_back(creds);

        if (err = mTLSCredentials->SubscribeCertChanged(mConfig.mCertStorage.c_str(), *this); !err.IsNone()) {
            return AOS_ERROR_WRAP(Error(err, "can't subscribe to certificate changes"));
        }
    } else {
        auto [creds, err] = mTLSCredentials->GetTLSClientCredentials();
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(Error(err, "can't get client credentials"));
        }

        if (creds) {
            mCredentialList.push_back(creds);
        } else {
            mCredentialList.push_back(grpc::InsecureChannelCredentials());
        }
    }

    if (auto err = mLogProvider->Subscribe(*this); !err.IsNone()) {
        return AOS_ERROR_WRAP(Error(err, "can't subscribe to log updates"));
    }

    mConnectionThread = std::thread(&SMClient::ConnectionLoop, this);

    return ErrorEnum::eNone;
}

Error SMClient::Stop()
{
    {
        std::lock_guard lock {mMutex};

        LOG_DBG() << "Stop SM client";

        if (mStopped) {
            return ErrorEnum::eNone;
        }

        mCertChangedThreadPool.Wait();
        mCertChangedThreadPool.Shutdown();

        mStopped = true;
        mStoppedCV.notify_all();

        mLogProvider->Unsubscribe(*this);

        if (mSecureConnection) {
            mTLSCredentials->UnsubscribeCertChanged(*this);
        }

        if (mCtx) {
            mCtx->TryCancel();
        }

        mCredentialList.clear();
    }

    if (mConnectionThread.joinable()) {
        mConnectionThread.join();
    }

    return ErrorEnum::eNone;
}

void SMClient::OnCertChanged(const iam::certhandler::CertInfo& info)
{
    (void)info;

    LOG_INF() << "Certificate changed";

    auto err = mCertChangedThreadPool.AddTask([this](void*) {
        std::lock_guard lock {mMutex};

        LOG_DBG() << "Handle certificate change";

        auto [creds, err] = mTLSCredentials->GetMTLSClientCredentials(mConfig.mCertStorage.c_str());
        if (!err.IsNone()) {
            LOG_ERR() << "Can't get client credentials: err=" << err;

            return;
        }

        mCredentialList.clear();
        mCredentialList.push_back(creds);

        mCredentialListUpdated = true;
    });

    if (!err.IsNone()) {
        LOG_ERR() << "Can't add cert changed task to thread pool: err=" << err;
    }
}

Error SMClient::SendMonitoringData(const monitoring::NodeMonitoringData& monitoringData)
{
    std::lock_guard lock {mMutex};

    LOG_INF() << "Send monitoring data";

    smproto::SMOutgoingMessages outgoingMessage;

    *outgoingMessage.mutable_instant_monitoring() = common::pbconvert::ConvertToProtoInstantMonitoring(monitoringData);

    if (!mStream || !mStream->Write(outgoingMessage)) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "can't send monitoring data"));
    }

    return ErrorEnum::eNone;
}

Error SMClient::SendAlert(const AlertVariant& alert)
{
    std::lock_guard lock {mMutex};

    LOG_INF() << "Send alert";
    LOG_DBG() << "Send alert: alert=" << alert;

    smproto::SMOutgoingMessages outgoingMessage;

    *outgoingMessage.mutable_alert() = common::pbconvert::ConvertToProto(alert);

    if (!mStream || !mStream->Write(outgoingMessage)) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "can't send alerts"));
    }

    return ErrorEnum::eNone;
}

Error SMClient::OnLogReceived(const cloudprotocol::PushLog& log)
{
    std::lock_guard lock {mMutex};

    LOG_INF() << "Send log";

    auto outgoingMessage = std::make_unique<smproto::SMOutgoingMessages>();

    *outgoingMessage->mutable_log() = common::pbconvert::ConvertToProto(log);

    if (!mStream || !mStream->Write(*outgoingMessage)) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "can't send log"));
    }

    return ErrorEnum::eNone;
}

Error SMClient::InstancesRunStatus(const Array<InstanceStatus>& instances)
{
    std::lock_guard lock {mMutex};

    if (!SendRunStatus(instances)) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "can't send run instances status"));
    }

    return ErrorEnum::eNone;
}

Error SMClient::InstancesUpdateStatus(const Array<InstanceStatus>& instances)
{
    std::lock_guard lock {mMutex};

    LOG_INF() << "Send update instances status";

    smproto::SMOutgoingMessages outgoingMessage;
    auto&                       response = *outgoingMessage.mutable_update_instances_status();

    for (const auto& instance : instances) {
        *response.add_instances() = common::pbconvert::ConvertToProto(instance);
    }

    if (!mStream || !mStream->Write(outgoingMessage)) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "can't send update instances status"));
    }

    return ErrorEnum::eNone;
}

Error SMClient::Subscribe(ConnectionSubscriberItf& subscriber)
{
    std::lock_guard lock {mMutex};

    if (std::find(mCloudConnectionSubscribers.begin(), mCloudConnectionSubscribers.end(), &subscriber)
        != mCloudConnectionSubscribers.end()) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "subscriber already exists"));
    }

    mCloudConnectionSubscribers.push_back(&subscriber);

    return ErrorEnum::eNone;
}

void SMClient::Unsubscribe(ConnectionSubscriberItf& subscriber)
{
    std::lock_guard lock {mMutex};

    mCloudConnectionSubscribers.erase(
        std::remove(mCloudConnectionSubscribers.begin(), mCloudConnectionSubscribers.end(), &subscriber),
        mCloudConnectionSubscribers.end());
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

std::unique_ptr<grpc::ClientContext> SMClient::CreateClientContext()
{
    return std::make_unique<grpc::ClientContext>();
}

SMClient::StubPtr SMClient::CreateStub(
    const std::string& url, const std::shared_ptr<grpc::ChannelCredentials>& credentials)
{
    auto channel = grpc::CreateCustomChannel(url, credentials, grpc::ChannelArguments());
    if (!channel) {
        LOG_ERR() << "Can't create client channel";

        return nullptr;
    }

    return smproto::SMService::NewStub(channel);
}

bool SMClient::SendNodeConfigStatus(const String& version, const Error& configErr)
{
    LOG_INF() << "Send node config status";

    if (!mStream) {
        return false;
    }

    smproto::SMOutgoingMessages outgoingMsg;
    auto&                       nodeConfigStatus = *outgoingMsg.mutable_node_config_status();

    common::pbconvert::SetErrorInfo(configErr, nodeConfigStatus);

    nodeConfigStatus.set_version(version.CStr());
    nodeConfigStatus.set_node_id(mNodeInfo.mNodeID.CStr());
    nodeConfigStatus.set_node_type(mNodeInfo.mNodeType.CStr());

    return mStream->Write(outgoingMsg);
}

bool SMClient::SendRunStatus(const Array<InstanceStatus>& instances)
{
    LOG_INF() << "Send run instances status";

    if (!mStream) {
        return false;
    }

    smproto::SMOutgoingMessages outgoingMessage;
    auto&                       response = *outgoingMessage.mutable_run_instances_status();

    for (const auto& instance : instances) {
        *response.add_instances() = common::pbconvert::ConvertToProto(instance);
    }

    return mStream->Write(outgoingMessage);
}

bool SMClient::RegisterSM(const std::string& url)
{
    std::lock_guard lock {mMutex};

    for (const auto& credentials : mCredentialList) {
        if (mStopped) {
            return false;
        }

        mStub = CreateStub(url, credentials);
        if (!mStub) {
            LOG_ERR() << "Can't create stub";

            continue;
        }

        mCtx    = CreateClientContext();
        mStream = mStub->RegisterSM(mCtx.get());
        if (!mStream) {
            LOG_ERR() << "Can't register SM";

            continue;
        }

        auto [version, configErr] = mResourceManager->GetNodeConfigVersion();

        if (!SendNodeConfigStatus(version, configErr)) {
            LOG_ERR() << "Can't send node config status";

            continue;
        }

        auto lastRunStatus = std::make_unique<InstanceStatusStaticArray>();

        if (auto err = mLauncher->GetCurrentRunStatus(*lastRunStatus); !err.IsNone()) {
            LOG_ERR() << "Can't get current run status: err=" << err;

            continue;
        }

        if (!SendRunStatus(*lastRunStatus)) {
            LOG_ERR() << "Can't send current run status";

            continue;
        }

        LOG_INF() << "Connection established";

        mCredentialListUpdated = false;

        return true;
    }

    return false;
}

void SMClient::ConnectionLoop() noexcept
{
    LOG_DBG() << "SM client connection thread started";

    while (true) {
        LOG_DBG() << "Connecting to SM server...";

        if (RegisterSM(mConfig.mCMServerURL)) {
            HandleIncomingMessages();

            LOG_DBG() << "SM client connection closed";
        }

        std::unique_lock lock {mMutex};

        mStoppedCV.wait_for(
            lock, std::chrono::nanoseconds(mConfig.mCMReconnectTimeout.Nanoseconds()), [this] { return mStopped; });

        if (mStopped) {
            break;
        }
    }

    LOG_DBG() << "SM client connection thread stopped";
}

void SMClient::HandleIncomingMessages() noexcept
{
    try {
        smproto::SMIncomingMessages incomingMsg;

        while (mStream->Read(&incomingMsg)) {
            bool ok = true;

            if (incomingMsg.has_get_node_config_status()) {
                ok = ProcessGetNodeConfigStatus();
            } else if (incomingMsg.has_check_node_config()) {
                ok = ProcessCheckNodeConfig(incomingMsg.check_node_config());
            } else if (incomingMsg.has_set_node_config()) {
                ok = ProcessSetNodeConfig(incomingMsg.set_node_config());
            } else if (incomingMsg.has_run_instances()) {
                ok = ProcessRunInstances(incomingMsg.run_instances());
            } else if (incomingMsg.has_update_networks()) {
                ok = ProcessUpdateNetworks(incomingMsg.update_networks());
            } else if (incomingMsg.has_system_log_request()) {
                ok = ProcessGetSystemLogRequest(incomingMsg.system_log_request());
            } else if (incomingMsg.has_instance_log_request()) {
                ok = ProcessGetInstanceLogRequest(incomingMsg.instance_log_request());
            } else if (incomingMsg.has_instance_crash_log_request()) {
                ok = ProcessGetInstanceCrashLogRequest(incomingMsg.instance_crash_log_request());
            } else if (incomingMsg.has_override_env_vars()) {
                ok = ProcessOverrideEnvVars(incomingMsg.override_env_vars());
            } else if (incomingMsg.has_get_average_monitoring()) {
                ok = ProcessGetAverageMonitoring();
            } else if (incomingMsg.has_connection_status()) {
                ok = ProcessConnectionStatus(incomingMsg.connection_status());
            } else {
                AOS_ERROR_CHECK_AND_THROW(ErrorEnum::eNotSupported, "not supported request type");
            }

            if (!ok) {
                break;
            }

            std::lock_guard lock {mMutex};

            if (mCredentialListUpdated) {
                LOG_DBG() << "Credential list updated: closing connection";

                mCtx->TryCancel();

                break;
            }
        }
    } catch (const std::exception& e) {
        LOG_ERR() << "Handle incoming messages failed: err=" << AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }
}

bool SMClient::ProcessGetNodeConfigStatus()
{
    LOG_INF() << "Process get node config status";

    auto [version, configErr] = mResourceManager->GetNodeConfigVersion();

    return SendNodeConfigStatus(version, configErr);
}

bool SMClient::ProcessCheckNodeConfig(const smproto::CheckNodeConfig& request)
{
    auto version    = String(request.version().c_str());
    auto nodeConfig = String(request.node_config().c_str());

    LOG_INF() << "Process check node config: version=" << version;

    auto configErr = mResourceManager->CheckNodeConfig(version, nodeConfig);

    return SendNodeConfigStatus(version, configErr);
}

bool SMClient::ProcessSetNodeConfig(const smproto::SetNodeConfig& request)
{
    auto version    = String(request.version().c_str());
    auto nodeConfig = String(request.node_config().c_str());

    LOG_INF() << "Process set node config: version=" << version;

    auto configErr = mResourceManager->UpdateNodeConfig(version, nodeConfig);

    return SendNodeConfigStatus(version, configErr);
}

bool SMClient::ProcessRunInstances(const smproto::RunInstances& request)
{
    LOG_INF() << "Process run instances";

    auto aosServices = std::make_unique<ServiceInfoStaticArray>();

    for (const auto& service : request.services()) {
        auto serviceInfo = std::make_unique<ServiceInfo>();

        if (auto err = common::pbconvert::ConvertToAos(service, *serviceInfo); !err.IsNone()) {
            LOG_ERR() << "Failed converting service info: err=" << err;
            return false;
        }

        if (auto err = aosServices->PushBack(*serviceInfo); !err.IsNone()) {
            LOG_ERR() << "Failed processing received service info: err=" << err;
            return false;
        }
    }

    auto aosLayers = std::make_unique<LayerInfoStaticArray>();

    for (const auto& layer : request.layers()) {
        auto layerInfo = std::make_unique<LayerInfo>();

        if (auto err = common::pbconvert::ConvertToAos(layer, *layerInfo); !err.IsNone()) {
            LOG_ERR() << "Failed converting layer info: err=" << err;
            return false;
        }

        if (auto err = aosLayers->PushBack(*layerInfo); !err.IsNone()) {
            LOG_ERR() << "Failed processing received layer info: err=" << err;

            return false;
        }
    }

    auto aosInstances = std::make_unique<InstanceInfoStaticArray>();

    for (const auto& instance : request.instances()) {
        auto instanceInfo = std::make_unique<InstanceInfo>();

        if (auto err = common::pbconvert::ConvertToAos(instance, *instanceInfo); !err.IsNone()) {
            LOG_ERR() << "Failed converting instance info: err=" << err;
            return false;
        }

        if (auto err = aosInstances->PushBack(*instanceInfo); !err.IsNone()) {
            LOG_ERR() << "Failed processing received instance info: err=" << err;
            return false;
        }
    }

    auto err = mLauncher->RunInstances(*aosServices, *aosLayers, *aosInstances, request.force_restart());
    if (!err.IsNone()) {
        LOG_ERR() << "Run instances failed: err=" << err;

        return false;
    }

    return true;
}

bool SMClient::ProcessUpdateNetworks(const smproto::UpdateNetworks& request)
{
    LOG_INF() << "Process update networks";

    std::vector<NetworkParameters> networkParams(request.networks_size());

    for (auto i = 0; i < request.networks_size(); ++i) {
        if (auto err = common::pbconvert::ConvertToAos(request.networks(i), networkParams[i]); !err.IsNone()) {
            LOG_ERR() << "Failed processing received network parameter: err=" << err;

            return false;
        }
    }

    if (auto err
        = mNetworkManager->UpdateNetworks(Array<aos::NetworkParameters>(networkParams.data(), networkParams.size()));
        !err.IsNone()) {
        LOG_ERR() << "Update networks failed: err=" << err;

        return false;
    }

    return true;
}

bool SMClient::ProcessGetSystemLogRequest(const smproto::SystemLogRequest& request)
{
    LOG_INF() << "Process get system log request: logID=" << request.log_id().c_str();

    auto logRequest = std::make_unique<aos::cloudprotocol::RequestLog>();

    if (auto err = common::pbconvert::ConvertToAos(request, *logRequest); !err.IsNone()) {
        LOG_ERR() << "Failed converting system log request: err=" << err;
        return false;
    }

    if (auto err = mLogProvider->GetSystemLog(*logRequest); !err.IsNone()) {
        LOG_ERR() << "Get system log failed: err=" << err;
        return false;
    }

    return true;
}

bool SMClient::ProcessGetInstanceLogRequest(const smproto::InstanceLogRequest& request)
{
    LOG_INF() << "Process get instance log request: logID=" << request.log_id().c_str();

    auto logRequest = std::make_unique<aos::cloudprotocol::RequestLog>();

    if (auto err = common::pbconvert::ConvertToAos(request, *logRequest); !err.IsNone()) {
        LOG_ERR() << "Failed converting instance log request: err=" << err;
        return false;
    }

    if (auto err = mLogProvider->GetInstanceLog(*logRequest); !err.IsNone()) {
        LOG_ERR() << "Get instance log failed: err=" << err;
        return false;
    }

    return true;
}

bool SMClient::ProcessGetInstanceCrashLogRequest(const smproto::InstanceCrashLogRequest& request)
{
    LOG_INF() << "Process get instance crash log request: logID=" << request.log_id().c_str();

    auto logRequest = std::make_unique<aos::cloudprotocol::RequestLog>();

    if (auto err = common::pbconvert::ConvertToAos(request, *logRequest); !err.IsNone()) {
        LOG_ERR() << "Failed converting instance crash log request: err=" << err;
        return false;
    }

    if (auto err = mLogProvider->GetInstanceCrashLog(*logRequest); !err.IsNone()) {
        LOG_ERR() << "Get instance crash log failed: err=" << err;
        return false;
    }

    return true;
}

bool SMClient::ProcessOverrideEnvVars(const smproto::OverrideEnvVars& request)
{
    LOG_INF() << "Process override env vars";

    auto                        envVarsInstanceInfos = std::make_unique<cloudprotocol::EnvVarsInstanceInfoArray>();
    smproto::SMOutgoingMessages outgoingMsg;

    auto& response = *outgoingMsg.mutable_override_env_var_status();

    auto err = common::pbconvert::ConvertToAos(request, *envVarsInstanceInfos);
    if (!err.IsNone()) {
        common::pbconvert::SetErrorInfo(err, response);

        return mStream->Write(outgoingMsg);
    }

    auto envVarStatuses = std::make_unique<cloudprotocol::EnvVarsInstanceStatusArray>();

    err = mLauncher->OverrideEnvVars(*envVarsInstanceInfos, *envVarStatuses);
    if (!err.IsNone()) {
        common::pbconvert::SetErrorInfo(err, response);

        return mStream->Write(outgoingMsg);
    }

    for (const auto& status : *envVarStatuses) {
        auto& envVarStatus = *response.add_env_vars_status();

        *envVarStatus.mutable_instance_filter() = common::pbconvert::ConvertToProto(status.mFilter);

        for (const auto& env : status.mStatuses) {
            *envVarStatus.add_statuses() = common::pbconvert::ConvertToProto(env);
        }
    }

    if (!mStream->Write(outgoingMsg)) {
        LOG_ERR() << "Can't send override env vars status: err=" << err;

        return false;
    }

    return true;
}

bool SMClient::ProcessGetAverageMonitoring()
{
    LOG_INF() << "Process get average monitoring";

    auto monitoringData = std::make_unique<monitoring::NodeMonitoringData>();

    if (auto err = mResourceMonitor->GetAverageMonitoringData(*monitoringData); !err.IsNone()) {
        LOG_ERR() << "Get average monitoring data failed: err=" << err;

        return false;
    }

    smproto::SMOutgoingMessages outgoingMsg;

    *outgoingMsg.mutable_average_monitoring() = common::pbconvert::ConvertToProtoAvarageMonitoring(*monitoringData);

    return mStream->Write(outgoingMsg);
}

bool SMClient::ProcessConnectionStatus(const smproto::ConnectionStatus& request)
{
    LOG_INF() << "Process connection status: cloudStatus=" << request.cloud_status();

    for (auto* subscriber : mCloudConnectionSubscribers) {
        if (request.cloud_status() == smproto::ConnectionEnum::CONNECTED) {
            subscriber->OnConnect();
        } else {
            subscriber->OnDisconnect();
        }
    }

    return true;
}

} // namespace aos::sm::smclient
