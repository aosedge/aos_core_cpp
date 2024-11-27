/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <Poco/Pipe.h>
#include <Poco/PipeStream.h>
#include <Poco/Process.h>
#include <Poco/StreamCopier.h>

#include <pbconvert/common.hpp>
#include <pbconvert/sm.hpp>
#include <utils/exception.hpp>
#include <utils/grpchelper.hpp>

#include "logger/logmodule.hpp"
#include "smclient.hpp"

namespace aos::sm::client {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error SMClient::Init(const config::Config& config, iam::provisionmanager::ProvisionManagerItf& provisionManager,
    crypto::CertLoaderItf& certLoader, crypto::x509::ProviderItf& cryptoProvider,
    iam::nodeinfoprovider::NodeInfoProviderItf& nodeInfoProvider,
    sm::resourcemanager::ResourceManagerItf& resourceManager, sm::networkmanager::NetworkManagerItf& networkManager,
    sm::logprovider::LogProviderItf& logProvider, monitoring::ResourceMonitorItf& resourceMonitor,
    sm::launcher::LauncherItf& launcher, bool secureConnection)
{
    LOG_DBG() << "Init SM client";

    mConfig           = config;
    mNodeInfoProvider = &nodeInfoProvider;
    mCertLoader       = &certLoader;
    mCryptoProvider   = &cryptoProvider;
    mProvisionManager = &provisionManager;
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

    if (mSecureConnection) {
        iam::certhandler::CertInfo certInfo;

        if (auto err = mProvisionManager->GetCert(String(mConfig.mCertStorage.c_str()), {}, {}, certInfo);
            !err.IsNone()) {
            return AOS_ERROR_WRAP(Error(err, "can't get certificate"));
        }

        if (auto err = mProvisionManager->SubscribeCertChanged(String(mConfig.mCertStorage.c_str()), *this);
            !err.IsNone()) {
            return AOS_ERROR_WRAP(Error(err, "can't subscribe to certificate changes"));
        }

        mCredentialList.push_back(
            common::utils::GetMTLSClientCredentials(certInfo, mConfig.mCACert.c_str(), *mCertLoader, *mCryptoProvider));
    } else {
        mCredentialList.push_back(grpc::InsecureChannelCredentials());
        if (!mConfig.mCACert.empty()) {
            mCredentialList.push_back(common::utils::GetTLSClientCredentials(mConfig.mCACert.c_str()));
        }
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
            return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "client stopped"));
        }

        mStopped = true;
        mStoppedCV.notify_all();

        if (mSecureConnection) {
            mProvisionManager->UnsubscribeCertChanged(*this);
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
    std::lock_guard lock {mMutex};

    LOG_INF() << "Certificate changed";

    mCredentialList.clear();
    mCredentialList.push_back(
        common::utils::GetMTLSClientCredentials(info, mConfig.mCACert.c_str(), *mCertLoader, *mCryptoProvider));

    mCredentialListUpdated = true;
}

Error SMClient::SendMonitoringData(const monitoring::NodeMonitoringData& monitoringData)
{
    std::lock_guard lock {mMutex};

    LOG_INF() << "Send monitoring data";

    smproto::SMOutgoingMessages outgoingMessage;
    *outgoingMessage.mutable_instant_monitoring() = common::pbconvert::ConvertToProtoInstantMonitoring(monitoringData);

    if (mStream && mStream->Write(outgoingMessage)) {
        return ErrorEnum::eNone;
    }

    return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "can't send monitoring data"));
}

Error SMClient::SendAlert(const cloudprotocol::AlertVariant& alert)
{
    std::lock_guard lock {mMutex};

    LOG_INF() << "Send alert";

    smproto::SMOutgoingMessages outgoingMessage;
    *outgoingMessage.mutable_alert() = common::pbconvert::ConvertToProto(alert);

    if (mStream && mStream->Write(outgoingMessage)) {
        return ErrorEnum::eNone;
    }

    return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "can't send alerts"));
}

Error SMClient::OnLogReceived(const cloudprotocol::PushLog& log)
{
    std::lock_guard lock {mMutex};

    LOG_INF() << "Send log";

    smproto::SMOutgoingMessages outgoingMessage;
    *outgoingMessage.mutable_log() = common::pbconvert::ConvertToProto(log);

    if (mStream && mStream->Write(outgoingMessage)) {
        return ErrorEnum::eNone;
    }

    return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "can't send log"));
}

Error SMClient::InstancesRunStatus(const Array<InstanceStatus>& instances)
{
    std::lock_guard lock {mMutex};

    LOG_INF() << "Send run instances status";

    smproto::SMOutgoingMessages outgoingMessage;
    auto&                       response = *outgoingMessage.mutable_run_instances_status();

    for (const auto& instance : instances) {
        *response.add_instances() = common::pbconvert::ConvertToProto(instance);
    }

    if (mStream && mStream->Write(outgoingMessage)) {
        return ErrorEnum::eNone;
    }

    return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "can't send run instances status"));
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

    if (mStream && mStream->Write(outgoingMessage)) {
        return ErrorEnum::eNone;
    }

    return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "can't send update instances status"));
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
    LOG_DBG() << "Send node config status";

    smproto::SMOutgoingMessages outgoingMsg;
    auto&                       nodeConfigStatus = *outgoingMsg.mutable_node_config_status();

    common::pbconvert::SetErrorInfo(configErr, nodeConfigStatus);

    nodeConfigStatus.set_version(version.CStr());
    nodeConfigStatus.set_node_id(mNodeInfo.mNodeID.CStr());
    nodeConfigStatus.set_node_type(mNodeInfo.mNodeType.CStr());

    return mStream->Write(outgoingMsg);
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

        LOG_DBG() << "Connection established";

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

        mStoppedCV.wait_for(lock, mConfig.mCMReconnectTimeout);

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
                AOS_ERROR_CHECK_AND_THROW("Not supported request type", ErrorEnum::eNotSupported);
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
        LOG_ERR() << e.what();
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

    ServiceInfoStaticArray aosServices;
    for (const auto& service : request.services()) {
        if (auto err = aosServices.PushBack(common::pbconvert::ConvertToAos(service)); !err.IsNone()) {
            LOG_ERR() << "Failed processing received service info: err=" << err;

            return false;
        }
    }

    LayerInfoStaticArray aosLayers;
    for (const auto& layer : request.layers()) {
        if (auto err = aosLayers.PushBack(common::pbconvert::ConvertToAos(layer)); !err.IsNone()) {
            LOG_ERR() << "Failed processing received layer info: err=" << err;

            return false;
        }
    }

    InstanceInfoStaticArray aosInstances;
    for (const auto& instance : request.instances()) {
        if (auto err = aosInstances.PushBack(common::pbconvert::ConvertToAos(instance)); !err.IsNone()) {
            LOG_ERR() << "Failed processing received instance info: err=" << err;

            return false;
        }
    }

    auto err = mLauncher->RunInstances(aosServices, aosLayers, aosInstances, request.force_restart());
    if (!err.IsNone()) {
        LOG_ERR() << "Run instances failed: err=" << err;

        return false;
    }

    return true;
}

bool SMClient::ProcessUpdateNetworks(const smproto::UpdateNetworks& request)
{
    StaticArray<NetworkParameters, cMaxNumNetworks> networkParams;

    for (const auto& network : request.networks()) {
        if (auto err = networkParams.PushBack(common::pbconvert::ConvertToAos(network)); !err.IsNone()) {
            LOG_ERR() << "Failed processing received network parameter: err=" << err;

            return false;
        }
    }

    if (auto err = mNetworkManager->UpdateNetworks(networkParams); !err.IsNone()) {
        LOG_ERR() << "Update networks failed: err=" << err;

        return false;
    }

    return true;
}

bool SMClient::ProcessGetSystemLogRequest(const smproto::SystemLogRequest& request)
{
    LOG_DBG() << "Process get system log request: logID=" << request.log_id().c_str();

    if (auto err = mLogProvider->GetSystemLog(common::pbconvert::ConvertToAos(request)); !err.IsNone()) {
        LOG_ERR() << "Get system log failed: err=" << err;

        return false;
    }

    return true;
}

bool SMClient::ProcessGetInstanceLogRequest(const smproto::InstanceLogRequest& request)
{
    LOG_DBG() << "Process get instance log request: logID=" << request.log_id().c_str();

    if (auto err = mLogProvider->GetInstanceLog(common::pbconvert::ConvertToAos(request)); !err.IsNone()) {
        LOG_ERR() << "Get instance log failed: err=" << err;

        return false;
    }

    return true;
}

bool SMClient::ProcessGetInstanceCrashLogRequest(const smproto::InstanceCrashLogRequest& request)
{
    LOG_DBG() << "Process get instance crash log request: logID=" << request.log_id().c_str();

    if (auto err = mLogProvider->GetInstanceCrashLog(common::pbconvert::ConvertToAos(request)); !err.IsNone()) {
        LOG_ERR() << "Get instance crash log failed: err=" << err;

        return false;
    }

    return true;
}

bool SMClient::ProcessOverrideEnvVars(const smproto::OverrideEnvVars& request)
{
    LOG_DBG() << "Process override env vars";

    cloudprotocol::EnvVarsInstanceInfoArray envVarsInstanceInfos;
    smproto::SMOutgoingMessages             outgoingMsg;

    auto& response = *outgoingMsg.mutable_override_env_var_status();

    auto err = common::pbconvert::ConvertToAos(request, envVarsInstanceInfos);

    if (!err.IsNone()) {
        common::pbconvert::SetErrorInfo(err, response);

        return mStream->Write(outgoingMsg);
    }

    cloudprotocol::EnvVarsInstanceStatusArray envVarStatuses;

    err = mLauncher->OverrideEnvVars(envVarsInstanceInfos, envVarStatuses);

    if (!err.IsNone()) {
        common::pbconvert::SetErrorInfo(err, response);

        return mStream->Write(outgoingMsg);
    }

    for (const auto& status : envVarStatuses) {
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

    monitoring::NodeMonitoringData monitoringData;

    if (auto err = mResourceMonitor->GetAverageMonitoringData(monitoringData); !err.IsNone()) {
        LOG_ERR() << "Get average monitoring data failed: err=" << err;

        return false;
    }

    smproto::SMOutgoingMessages outgoingMsg;
    *outgoingMsg.mutable_average_monitoring() = common::pbconvert::ConvertToProtoAvarageMonitoring(monitoringData);

    return mStream->Write(outgoingMsg);
}

bool SMClient::ProcessConnectionStatus(const smproto::ConnectionStatus& request)
{
    LOG_DBG() << "Process connection status: cloudStatus=" << request.cloud_status();

    if (auto err = mLauncher->SetCloudConnection(request.cloud_status() == smproto::ConnectionEnum::CONNECTED);
        !err.IsNone()) {
        LOG_ERR() << "Set cloud connection failed: err=" << err;

        return false;
    }

    return true;
}

} // namespace aos::sm::client
