/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <algorithm>

#include <common/pbconvert/sm.hpp>
#include <sm/logger/logmodule.hpp>

#include "smclient.hpp"

namespace aos::sm::smclient {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error SMClient::Init(const Config& config, const std::string& nodeID,
    aos::common::iamclient::TLSCredentialsItf& tlsCredentials, aos::iamclient::CertProviderItf& certProvider,
    launcher::RuntimeInfoProviderItf&         runtimeInfoProvider,
    resourcemanager::ResourceInfoProviderItf& resourceInfoProvider, nodeconfig::NodeConfigHandlerItf& nodeConfigHandler,
    launcher::LauncherItf& launcher, logging::LogProviderItf& logProvider,
    networkmanager::NetworkManagerItf& networkManager, aos::monitoring::MonitoringItf& monitoring,
    aos::instancestatusprovider::ProviderItf& instanceStatusProvider, aos::nodeconfig::JSONProviderItf& jsonProvider,
    bool secureConnection)
{
    LOG_DBG() << "Init SM client";

    mConfig                 = config;
    mNodeID                 = nodeID;
    mTLSCredentials         = &tlsCredentials;
    mCertProvider           = &certProvider;
    mRuntimeInfoProvider    = &runtimeInfoProvider;
    mResourceInfoProvider   = &resourceInfoProvider;
    mNodeConfigHandler      = &nodeConfigHandler;
    mLauncher               = &launcher;
    mLogProvider            = &logProvider;
    mNetworkManager         = &networkManager;
    mMonitoring             = &monitoring;
    mInstanceStatusProvider = &instanceStatusProvider;
    mJSONProvider           = &jsonProvider;
    mSecureConnection       = secureConnection;

    return ErrorEnum::eNone;
}

Error SMClient::Start()
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Start SM client";

    if (!mStopped) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "client already started"));
    }

    if (mSecureConnection) {
        auto [creds, err] = mTLSCredentials->GetMTLSClientCredentials(mConfig.mCertStorage.c_str());
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(Error(err, "can't get MTLS client credentials"));
        }

        mCredentials = std::move(creds);

        if (err = mCertProvider->SubscribeListener(mConfig.mCertStorage.c_str(), *this); !err.IsNone()) {
            return AOS_ERROR_WRAP(Error(err, "can't subscribe to certificate changes"));
        }
    } else {
        auto [creds, err] = mTLSCredentials->GetTLSClientCredentials();
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(Error(err, "can't get TLS client credentials"));
        }

        mCredentials = std::move(creds);
    }

    mStopped = false;

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

        mStopped = true;
        mStoppedCV.notify_all();

        if (mSecureConnection) {
            mCertProvider->UnsubscribeListener(*this);
        }

        if (mCtx) {
            mCtx->TryCancel();
        }
    }

    if (mConnectionThread.joinable()) {
        mConnectionThread.join();
    }

    return ErrorEnum::eNone;
}

void SMClient::OnCertChanged(const CertInfo& info)
{
    (void)info;

    std::lock_guard lock {mMutex};

    LOG_INF() << "Certificate changed";

    auto [creds, err] = mTLSCredentials->GetMTLSClientCredentials(mConfig.mCertStorage.c_str());
    if (!err.IsNone()) {
        LOG_ERR() << "Can't get client credentials: err=" << err;

        return;
    }

    mCredentials = std::move(creds);

    LOG_DBG() << "Credentials updated";
}

Error SMClient::SendAlert(const AlertVariant& alert)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Send alert" << Log::Field("alert", alert);

    if (!mStream) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "stream not available"));
    }

    smproto::SMOutgoingMessages outgoingMsg;
    common::pbconvert::ConvertToProto(alert, *outgoingMsg.mutable_alert());

    if (!mStream->Write(outgoingMsg)) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "can't send alert"));
    }

    return ErrorEnum::eNone;
}

Error SMClient::SendMonitoringData(const aos::monitoring::NodeMonitoringData& monitoringData)
{
    std::lock_guard lock {mMutex};

    LOG_INF() << "Send monitoring data";

    if (!mStream) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "stream not available"));
    }

    smproto::SMOutgoingMessages outgoingMsg;
    common::pbconvert::ConvertToProto(monitoringData, *outgoingMsg.mutable_instant_monitoring());

    if (!mStream->Write(outgoingMsg)) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "can't send monitoring data"));
    }

    return ErrorEnum::eNone;
}

Error SMClient::SendLog(const PushLog& log)
{
    std::lock_guard lock {mMutex};

    LOG_INF() << "Send log";

    if (!mStream) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "stream not available"));
    }

    smproto::SMOutgoingMessages outgoingMsg;
    common::pbconvert::ConvertToProto(log, *outgoingMsg.mutable_log());

    if (!mStream->Write(outgoingMsg)) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "can't send log"));
    }

    return ErrorEnum::eNone;
}

Error SMClient::SendNodeInstancesStatuses(const Array<aos::InstanceStatus>& statuses)
{
    std::lock_guard lock {mMutex};

    if (!mStream) {
        return Error(ErrorEnum::eFailed, "stream not available");
    }

    smproto::SMOutgoingMessages outgoingMsg;
    auto&                       nodeStatus = *outgoingMsg.mutable_node_instances_status();

    for (const auto& status : statuses) {
        common::pbconvert::ConvertToProto(status, *nodeStatus.add_instances());
    }

    if (!mStream->Write(outgoingMsg)) {
        return Error(ErrorEnum::eFailed, "can't send node instances statuses");
    }

    return ErrorEnum::eNone;
}

Error SMClient::SendUpdateInstancesStatuses(const Array<aos::InstanceStatus>& statuses)
{
    std::lock_guard lock {mMutex};

    if (!mStream) {
        return Error(ErrorEnum::eFailed, "stream not available");
    }

    smproto::SMOutgoingMessages outgoingMsg;
    auto&                       updateStatus = *outgoingMsg.mutable_update_instances_status();

    for (const auto& status : statuses) {
        common::pbconvert::ConvertToProto(status, *updateStatus.add_instances());
    }

    if (!mStream->Write(outgoingMsg)) {
        return Error(ErrorEnum::eFailed, "can't send update instances statuses");
    }

    return ErrorEnum::eNone;
}

Error SMClient::GetBlobsInfo(const Array<StaticString<oci::cDigestLen>>& digests, Array<StaticString<cURLLen>>& urls)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Get blobs info" << Log::Field("count", digests.Size());

    if (!mStub) {
        return Error(ErrorEnum::eFailed, "stub not available");
    }

    smproto::BlobsInfosRequest request;

    for (const auto& digest : digests) {
        request.add_digests(digest.CStr());
    }

    grpc::ClientContext context;
    smproto::BlobsInfos response;

    auto status = mStub->GetBlobsInfos(&context, request, &response);
    if (!status.ok()) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, status.error_message().c_str()));
    }

    for (const auto& url : response.urls()) {
        if (auto err = urls.PushBack(url.c_str()); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    return ErrorEnum::eNone;
}

Error SMClient::SubscribeListener(aos::cloudconnection::ConnectionListenerItf& listener)
{
    std::lock_guard lock {mMutex};

    auto it = std::find(mConnectionListeners.begin(), mConnectionListeners.end(), &listener);
    if (it != mConnectionListeners.end()) {
        return Error(ErrorEnum::eAlreadyExist, "listener already subscribed");
    }

    mConnectionListeners.push_back(&listener);

    return ErrorEnum::eNone;
}

Error SMClient::UnsubscribeListener(aos::cloudconnection::ConnectionListenerItf& listener)
{
    std::lock_guard lock {mMutex};

    auto it = std::find(mConnectionListeners.begin(), mConnectionListeners.end(), &listener);
    if (it == mConnectionListeners.end()) {
        return Error(ErrorEnum::eNotFound, "listener not found");
    }

    mConnectionListeners.erase(it);

    return ErrorEnum::eNone;
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

bool SMClient::SendSMInfo()
{
    LOG_DBG() << "Send SM info";

    if (!mStream) {
        return false;
    }

    smproto::SMOutgoingMessages outgoingMsg;
    auto&                       smInfo = *outgoingMsg.mutable_sm_info();

    smInfo.set_node_id(mNodeID);

    auto runtimes = std::make_unique<RuntimeInfoArray>();
    if (auto err = mRuntimeInfoProvider->GetRuntimesInfos(*runtimes); !err.IsNone()) {
        LOG_ERR() << "Can't get runtimes info: err=" << err;

        return false;
    }

    for (const auto& runtime : *runtimes) {
        common::pbconvert::ConvertToProto(runtime, *smInfo.add_runtimes());
    }

    auto resources = std::make_unique<StaticArray<ResourceInfo, cMaxNumNodeResources>>();
    if (auto err = mResourceInfoProvider->GetResourcesInfos(*resources); !err.IsNone()) {
        LOG_ERR() << "Can't get resources info: err=" << err;

        return false;
    }

    for (const auto& resource : *resources) {
        common::pbconvert::ConvertToProto(resource, *smInfo.add_resources());
    }

    return mStream->Write(outgoingMsg);
}

bool SMClient::SendNodeInstancesStatus()
{
    LOG_DBG() << "Send node instances status";

    if (!mStream) {
        return false;
    }

    smproto::SMOutgoingMessages outgoingMsg;
    auto&                       nodeStatus = *outgoingMsg.mutable_node_instances_status();

    auto statuses = std::make_unique<InstanceStatusArray>();
    if (auto err = mInstanceStatusProvider->GetInstancesStatuses(*statuses); !err.IsNone()) {
        LOG_ERR() << "Can't get instances statuses: err=" << err;

        return false;
    }

    for (const auto& status : *statuses) {
        common::pbconvert::ConvertToProto(status, *nodeStatus.add_instances());
    }

    return mStream->Write(outgoingMsg);
}

bool SMClient::RegisterSM(const std::string& url)
{
    std::lock_guard lock {mMutex};

    if (mStopped) {
        return false;
    }

    mStub = CreateStub(url, mCredentials);
    if (!mStub) {
        LOG_ERR() << "Can't create stub";

        return false;
    }

    mCtx = CreateClientContext();

    mStream = mStub->RegisterSM(mCtx.get());
    if (!mStream) {
        LOG_ERR() << "Can't register SM";

        return false;
    }

    LOG_INF() << "Connection established";

    return true;
}

void SMClient::HandleIncomingMessages()
{
    smproto::SMIncomingMessages incomingMsg;

    while (mStream->Read(&incomingMsg)) {
        if (incomingMsg.has_get_node_config_status()) {
            if (auto err = ProcessGetNodeConfigStatus(); !err.IsNone()) {
                LOG_ERR() << "Failed to process get node config status: err=" << err;
            }
        } else if (incomingMsg.has_check_node_config()) {
            if (auto err = ProcessCheckNodeConfig(incomingMsg.check_node_config()); !err.IsNone()) {
                LOG_ERR() << "Failed to process check node config: err=" << err;
            }
        } else if (incomingMsg.has_set_node_config()) {
            if (auto err = ProcessSetNodeConfig(incomingMsg.set_node_config()); !err.IsNone()) {
                LOG_ERR() << "Failed to process set node config: err=" << err;
            }
        } else if (incomingMsg.has_update_instances()) {
            if (auto err = ProcessUpdateInstances(incomingMsg.update_instances()); !err.IsNone()) {
                LOG_ERR() << "Failed to process update instances: err=" << err;
            }
        } else if (incomingMsg.has_system_log_request()) {
            if (auto err = ProcessSystemLogRequest(incomingMsg.system_log_request()); !err.IsNone()) {
                LOG_ERR() << "Failed to process system log request: err=" << err;
            }
        } else if (incomingMsg.has_instance_log_request()) {
            if (auto err = ProcessInstanceLogRequest(incomingMsg.instance_log_request()); !err.IsNone()) {
                LOG_ERR() << "Failed to process instance log request: err=" << err;
            }
        } else if (incomingMsg.has_instance_crash_log_request()) {
            if (auto err = ProcessInstanceCrashLogRequest(incomingMsg.instance_crash_log_request()); !err.IsNone()) {
                LOG_ERR() << "Failed to process instance crash log request: err=" << err;
            }
        } else if (incomingMsg.has_get_average_monitoring()) {
            if (auto err = ProcessGetAverageMonitoring(); !err.IsNone()) {
                LOG_ERR() << "Failed to process get average monitoring: err=" << err;
            }
        } else if (incomingMsg.has_connection_status()) {
            if (auto err = ProcessConnectionStatus(incomingMsg.connection_status()); !err.IsNone()) {
                LOG_ERR() << "Failed to process connection status: err=" << err;
            }
        } else if (incomingMsg.has_update_networks()) {
            if (auto err = ProcessUpdateNetworks(incomingMsg.update_networks()); !err.IsNone()) {
                LOG_ERR() << "Failed to process update networks: err=" << err;
            }
        }
    }
}

Error SMClient::ProcessGetNodeConfigStatus()
{
    LOG_DBG() << "Process get node config status";

    NodeConfigStatus status;

    if (auto err = mNodeConfigHandler->GetNodeConfigStatus(status); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    smproto::SMOutgoingMessages outgoingMsg;

    common::pbconvert::ConvertToProto(status, *outgoingMsg.mutable_node_config_status());

    std::lock_guard lock {mMutex};

    if (!mStream->Write(outgoingMsg)) {
        return Error(ErrorEnum::eFailed, "failed to send node config status");
    }

    return ErrorEnum::eNone;
}

Error SMClient::ProcessCheckNodeConfig(const smproto::CheckNodeConfig& checkConfig)
{
    LOG_DBG() << "Process check node config";

    auto nodeConfig = std::make_unique<NodeConfig>();

    if (auto err = mJSONProvider->NodeConfigFromJSON(checkConfig.node_config().c_str(), *nodeConfig); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    nodeConfig->mVersion = checkConfig.version().c_str();

    NodeConfigStatus status;

    if (auto err = mNodeConfigHandler->GetNodeConfigStatus(status); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    status.mError   = mNodeConfigHandler->CheckNodeConfig(*nodeConfig);
    status.mVersion = nodeConfig->mVersion;

    smproto::SMOutgoingMessages outgoingMsg;

    common::pbconvert::ConvertToProto(status, *outgoingMsg.mutable_node_config_status());

    std::lock_guard lock {mMutex};

    if (!mStream->Write(outgoingMsg)) {
        return Error(ErrorEnum::eFailed, "failed to send node config status");
    }

    return ErrorEnum::eNone;
}

Error SMClient::ProcessSetNodeConfig(const smproto::SetNodeConfig& setConfig)
{
    LOG_DBG() << "Process set node config";

    auto nodeConfig = std::make_unique<NodeConfig>();

    if (auto err = mJSONProvider->NodeConfigFromJSON(setConfig.node_config().c_str(), *nodeConfig); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    nodeConfig->mVersion = setConfig.version().c_str();

    NodeConfigStatus status;

    if (auto err = mNodeConfigHandler->GetNodeConfigStatus(status); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    status.mError   = mNodeConfigHandler->UpdateNodeConfig(*nodeConfig);
    status.mVersion = nodeConfig->mVersion;

    smproto::SMOutgoingMessages outgoingMsg;

    common::pbconvert::ConvertToProto(status, *outgoingMsg.mutable_node_config_status());

    std::lock_guard lock {mMutex};

    if (!mStream->Write(outgoingMsg)) {
        return Error(ErrorEnum::eFailed, "failed to send node config status");
    }

    return ErrorEnum::eNone;
}

Error SMClient::ProcessUpdateInstances(const smproto::UpdateInstances& updateInstances)
{
    LOG_DBG() << "Process update instances";

    auto stopInstances  = std::make_unique<StaticArray<InstanceIdent, cMaxNumInstances>>();
    auto startInstances = std::make_unique<InstanceInfoArray>();

    if (auto err = common::pbconvert::ConvertFromProto(updateInstances, *stopInstances, *startInstances);
        !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = mLauncher->UpdateInstances(*stopInstances, *startInstances); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error SMClient::ProcessSystemLogRequest(const smproto::SystemLogRequest& request)
{
    LOG_DBG() << "Process system log request";

    RequestLog requestLog;

    if (auto err = common::pbconvert::ConvertFromProto(request, requestLog); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = mLogProvider->GetSystemLog(requestLog); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error SMClient::ProcessInstanceLogRequest(const smproto::InstanceLogRequest& request)
{
    LOG_DBG() << "Process instance log request";

    RequestLog requestLog;

    if (auto err = common::pbconvert::ConvertFromProto(request, requestLog); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = mLogProvider->GetInstanceLog(requestLog); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error SMClient::ProcessInstanceCrashLogRequest(const smproto::InstanceCrashLogRequest& request)
{
    LOG_DBG() << "Process instance crash log request";

    RequestLog requestLog;

    if (auto err = common::pbconvert::ConvertFromProto(request, requestLog); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = mLogProvider->GetInstanceCrashLog(requestLog); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error SMClient::ProcessGetAverageMonitoring()
{
    LOG_DBG() << "Process get average monitoring";

    auto monitoringData = std::make_unique<aos::monitoring::NodeMonitoringData>();

    if (auto err = mMonitoring->GetAverageMonitoringData(*monitoringData); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    smproto::SMOutgoingMessages outgoingMsg;
    common::pbconvert::ConvertToProto(*monitoringData, *outgoingMsg.mutable_average_monitoring());

    std::lock_guard lock {mMutex};

    if (!mStream->Write(outgoingMsg)) {
        return Error(ErrorEnum::eFailed, "failed to send average monitoring");
    }

    return ErrorEnum::eNone;
}

Error SMClient::ProcessConnectionStatus(const smproto::ConnectionStatus& status)
{
    LOG_DBG() << "Process connection status: " << status.cloud_status();

    std::lock_guard lock {mMutex};

    for (auto* listener : mConnectionListeners) {
        if (status.cloud_status() == smproto::ConnectionEnum::CONNECTED) {
            listener->OnConnect();
        } else {
            listener->OnDisconnect();
        }
    }

    return ErrorEnum::eNone;
}

Error SMClient::ProcessUpdateNetworks(const smproto::UpdateNetworks& updateNetworks)
{
    LOG_DBG() << "Process update networks";

    auto networks = std::make_unique<StaticArray<NetworkParameters, cMaxNumOwners>>();

    if (auto err = common::pbconvert::ConvertFromProto(updateNetworks, *networks); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = mNetworkManager->UpdateNetworks(*networks); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

void SMClient::ConnectionLoop() noexcept
{
    LOG_DBG() << "SM client connection thread started";

    while (true) {
        LOG_DBG() << "Connecting to SM server...";

        if (RegisterSM(mConfig.mCMServerURL)) {
            if (!SendSMInfo()) {
                LOG_ERR() << "Can't send SM info";
            } else if (!SendNodeInstancesStatus()) {
                LOG_ERR() << "Can't send node instances status";
            } else {
                HandleIncomingMessages();
            }

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

} // namespace aos::sm::smclient
