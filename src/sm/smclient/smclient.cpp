#include <common/utils/grpchelper.hpp>
/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <algorithm>

#include <core/common/tools/logger.hpp>

#include <common/pbconvert/common.hpp>
#include <common/pbconvert/sm.hpp>

#include "smclient.hpp"

namespace aos::sm::smclient {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error SMClient::Init(const Config& config, const std::string& nodeID,
    aos::common::iamclient::TLSCredentialsItf& tlsCredentials, aos::iamclient::CertProviderItf& certProvider,
    launcher::RuntimeInfoProviderItf&         runtimeInfoProvider,
    resourcemanager::ResourceInfoProviderItf& resourceInfoProvider, nodeconfig::NodeConfigHandlerItf& nodeConfigHandler,
    launcher::LauncherItf& launcher, logging::LogProviderItf& logProvider, aos::monitoring::MonitoringItf& monitoring,
    aos::instancestatusprovider::ProviderItf& instanceStatusProvider, aos::nodeconfig::JSONProviderItf& jsonProvider,
    aos::networkmanager::PendingUpdateHandlerItf& networkUpdateHandler, bool secureConnection)
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
    mMonitoring             = &monitoring;
    mInstanceStatusProvider = &instanceStatusProvider;
    mJSONProvider           = &jsonProvider;
    mNetworkUpdateHandler   = &networkUpdateHandler;
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

    if (auto err = CreateCredentials(); !err.IsNone()) {
        return err;
    }

    if (mSecureConnection) {
        if (auto err = mCertProvider->SubscribeListener(mConfig.mCertStorage.c_str(), *this); !err.IsNone()) {
            return AOS_ERROR_WRAP(Error(err, "can't subscribe to certificate changes"));
        }
    }

    mStopped = false;

    StartNetworkUpdateSubscription();
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

        if (mNetworkUpdateCtx) {
            mNetworkUpdateCtx->TryCancel();
        }

        mNetworkUpdateCV.notify_all();
    }

    if (mConnectionThread.joinable()) {
        mConnectionThread.join();
    }

    if (mNetworkUpdateThread.joinable()) {
        mNetworkUpdateThread.join();
    }

    return ErrorEnum::eNone;
}

void SMClient::OnCertChanged(const CertInfo& info)
{
    (void)info;

    std::lock_guard lock {mMutex};

    if (mStopped) {
        return;
    }

    LOG_INF() << "Certificate changed";

    if (mCtx) {
        mCtx->TryCancel();
    }
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

bool SMClient::IsConnected() const
{
    std::lock_guard lock {mMutex};

    return mConnectionStatus == servicemanager::v5::ConnectionEnum::CONNECTED;
}

Error SMClient::GetNodeNetworkParams(const String& networkID, const String& nodeID, NetworkParams& result)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "GetNodeNetworkParams" << Log::Field("networkID", networkID) << Log::Field("nodeID", nodeID);

    if (!mNetworkStub) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "network stub not available"));
    }

    smproto::GetNodeNetworkParamsRequest request;
    request.set_network_id(networkID.CStr());
    request.set_node_id(nodeID.CStr());

    grpc::ClientContext                   context;
    smproto::GetNodeNetworkParamsResponse response;

    auto status = mNetworkStub->GetNodeNetworkParams(&context, request, &response);
    if (!status.ok()) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, status.error_message().c_str()));
    }

    if (response.has_error()) {
        return common::pbconvert::ConvertFromProto(response.error());
    }

    result.mNetworkID = networkID;
    result.mSubnet    = response.subnet().c_str();
    result.mIP        = response.ip().c_str();
    result.mVlanID    = response.vlan_id();

    return ErrorEnum::eNone;
}

Error SMClient::AllocateInstanceNetwork(const InstanceIdent& instance, const String& networkID, const String& nodeID,
    const UpdateItemNetworkParams& serviceData, InstanceNetworkAllocation& result)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "AllocateInstanceNetwork" << Log::Field("networkID", networkID) << Log::Field("nodeID", nodeID);

    if (!mNetworkStub) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "network stub not available"));
    }

    smproto::AllocateInstanceNetworkRequest request;
    *request.mutable_instance() = common::pbconvert::ConvertToProto(instance);
    request.set_network_id(networkID.CStr());
    request.set_node_id(nodeID.CStr());

    for (const auto& host : serviceData.mHosts) {
        request.mutable_service_data()->add_hosts(host.CStr());
    }

    for (const auto& connection : serviceData.mAllowedConnections) {
        request.mutable_service_data()->add_allowed_connections(connection.CStr());
    }

    for (const auto& port : serviceData.mExposedPorts) {
        request.mutable_service_data()->add_exposed_ports(port.CStr());
    }

    grpc::ClientContext                      context;
    smproto::AllocateInstanceNetworkResponse response;

    auto status = mNetworkStub->AllocateInstanceNetwork(&context, request, &response);
    if (!status.ok()) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, status.error_message().c_str()));
    }

    if (response.has_error()) {
        return common::pbconvert::ConvertFromProto(response.error());
    }

    if (auto err = common::pbconvert::ConvertFromProto(response, result); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error SMClient::ReleaseInstanceNetwork(const InstanceIdent& instance, const String& nodeID)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "ReleaseInstanceNetwork" << Log::Field("nodeID", nodeID);

    if (!mNetworkStub) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "network stub not available"));
    }

    smproto::ReleaseInstanceNetworkRequest request;
    *request.mutable_instance() = common::pbconvert::ConvertToProto(instance);
    request.set_node_id(nodeID.CStr());

    grpc::ClientContext                     context;
    smproto::ReleaseInstanceNetworkResponse response;

    auto status = mNetworkStub->ReleaseInstanceNetwork(&context, request, &response);
    if (!status.ok()) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, status.error_message().c_str()));
    }

    if (response.has_error()) {
        return common::pbconvert::ConvertFromProto(response.error());
    }

    return ErrorEnum::eNone;
}

Error SMClient::ReleaseNodeNetwork(const String& networkID, const String& nodeID)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "ReleaseNodeNetwork" << Log::Field("networkID", networkID) << Log::Field("nodeID", nodeID);

    if (!mNetworkStub) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "network stub not available"));
    }

    smproto::ReleaseNodeNetworkRequest request;
    request.set_network_id(networkID.CStr());
    request.set_node_id(nodeID.CStr());

    grpc::ClientContext                 context;
    smproto::ReleaseNodeNetworkResponse response;

    auto status = mNetworkStub->ReleaseNodeNetwork(&context, request, &response);
    if (!status.ok()) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, status.error_message().c_str()));
    }

    if (response.has_error()) {
        return common::pbconvert::ConvertFromProto(response.error());
    }

    return ErrorEnum::eNone;
}

Error SMClient::SyncNetworkState(const String& nodeID, const Array<InstanceNetworkStateInfo>& instances)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Sync network state" << Log::Field("nodeID", nodeID) << Log::Field("instancesCount", instances.Size());

    if (!mNetworkStub) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "network stub not available"));
    }

    smproto::SyncNetworkStateRequest request;
    request.set_node_id(nodeID.CStr());

    for (const auto& instance : instances) {
        if (auto err = common::pbconvert::ConvertToProto(instance, *request.add_instances()); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    grpc::ClientContext               context;
    smproto::SyncNetworkStateResponse response;

    auto status = mNetworkStub->SyncNetworkState(&context, request, &response);
    if (!status.ok()) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, status.error_message().c_str()));
    }

    if (response.has_error()) {
        return common::pbconvert::ConvertFromProto(response.error());
    }

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

std::unique_ptr<grpc::ClientContext> SMClient::CreateClientContext()
{
    return std::make_unique<grpc::ClientContext>();
}

Error SMClient::CreateCredentials()
{
    if (mSecureConnection) {
        auto [creds, err] = mTLSCredentials->GetMTLSClientCredentials(mConfig.mCertStorage.c_str());
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(Error(err, "can't get MTLS client credentials"));
        }

        mCredentials = std::move(creds);

        return ErrorEnum::eNone;
    }

    auto [creds, err] = mTLSCredentials->GetTLSClientCredentials();
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(Error(err, "can't get TLS client credentials"));
    }

    mCredentials = std::move(creds);

    return ErrorEnum::eNone;
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

    if (auto err = CreateCredentials(); !err.IsNone()) {
        LOG_ERR() << "Can't create SM client credentials" << Log::Field(err);

        return false;
    }

    auto channel = grpc::CreateCustomChannel(url, mCredentials, common::utils::CreateGRPCChannelArguments());
    if (!channel) {
        LOG_ERR() << "Can't create client channel";

        return false;
    }

    if (mStub = smproto::SMService::NewStub(channel); !mStub) {
        LOG_ERR() << "Can't create SM stub";

        return false;
    }

    if (mNetworkStub = smproto::NetworkService::NewStub(channel); !mNetworkStub) {
        LOG_ERR() << "Can't create network stub";

        return false;
    }

    mNetworkUpdateCV.notify_all();

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
        Error err;

        if (incomingMsg.has_get_node_config_status()) {
            err = ProcessGetNodeConfigStatus();
        } else if (incomingMsg.has_check_node_config()) {
            err = ProcessCheckNodeConfig(incomingMsg.check_node_config());
        } else if (incomingMsg.has_set_node_config()) {
            err = ProcessSetNodeConfig(incomingMsg.set_node_config());
        } else if (incomingMsg.has_update_instances()) {
            err = ProcessUpdateInstances(incomingMsg.update_instances());
        } else if (incomingMsg.has_system_log_request()) {
            err = ProcessSystemLogRequest(incomingMsg.system_log_request());
        } else if (incomingMsg.has_instance_log_request()) {
            err = ProcessInstanceLogRequest(incomingMsg.instance_log_request());
        } else if (incomingMsg.has_instance_crash_log_request()) {
            err = ProcessInstanceCrashLogRequest(incomingMsg.instance_crash_log_request());
        } else if (incomingMsg.has_get_average_monitoring()) {
            err = ProcessGetAverageMonitoring();
        } else if (incomingMsg.has_connection_status()) {
            err = ProcessConnectionStatus(incomingMsg.connection_status());
        }

        if (!err.IsNone()) {
            LOG_ERR() << "Failed to process incoming message" << Log::Field(err);
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
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Process connection status" << Log::Field("status", status.cloud_status());

    if (mConnectionStatus.has_value() && mConnectionStatus == status.cloud_status()) {
        return ErrorEnum::eNone;
    }

    mConnectionStatus = status.cloud_status();

    for (auto* listener : mConnectionListeners) {
        if (mConnectionStatus == servicemanager::v5::ConnectionEnum::CONNECTED) {
            listener->OnConnect();
        } else {
            listener->OnDisconnect();
        }
    }

    return ErrorEnum::eNone;
}

void SMClient::ConnectionLoop() noexcept
{
    LOG_DBG() << "SM client connection thread started";

    while (true) {
        LOG_DBG() << "Connecting to SM server" << Log::Field("url", mConfig.mCMServerURL.c_str())
                  << Log::Field("secure", mSecureConnection);

        if (RegisterSM(mConfig.mCMServerURL)) {
            if (!SendSMInfo()) {
                LOG_ERR() << "Can't send SM info";
            } else if (!SendNodeInstancesStatus()) {
                LOG_ERR() << "Can't send node instances status";
            } else {
                std::vector<aos::sm::smclient::ConnectListenerItf*> listeners;

                {
                    std::lock_guard lock {mMutex};
                    listeners = mConnectListeners;
                }

                for (auto* listener : listeners) {
                    listener->OnConnect();
                }

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

Error SMClient::SubscribeListener(ConnectListenerItf& listener)
{
    std::lock_guard lock {mMutex};

    auto it = std::find(mConnectListeners.begin(), mConnectListeners.end(), &listener);
    if (it != mConnectListeners.end()) {
        return Error(ErrorEnum::eAlreadyExist, "listener already subscribed");
    }

    mConnectListeners.push_back(&listener);

    return ErrorEnum::eNone;
}

Error SMClient::UnsubscribeListener(ConnectListenerItf& listener)
{
    std::lock_guard lock {mMutex};

    auto it = std::find(mConnectListeners.begin(), mConnectListeners.end(), &listener);
    if (it != mConnectListeners.end()) {
        mConnectListeners.erase(it);
    }

    return ErrorEnum::eNone;
}

void SMClient::StartNetworkUpdateSubscription()
{
    mNetworkUpdateThread = std::thread([this]() {
        LOG_DBG() << "Network update subscription thread started";

        for (;;) {
            {
                std::unique_lock lock {mMutex};

                mNetworkUpdateCV.wait(lock, [this]() { return mNetworkStub != nullptr || mStopped; });

                if (mStopped) {
                    break;
                }

                mNetworkUpdateCtx = CreateClientContext();

                smproto::SubscribeInstanceNetworkUpdatesRequest request;
                request.set_node_id(mNodeID);

                mNetworkUpdateReader = mNetworkStub->SubscribeInstanceNetworkUpdates(mNetworkUpdateCtx.get(), request);
            }

            if (mNetworkUpdateReader) {
                smproto::InstanceNetworkUpdateNotification notification;

                while (mNetworkUpdateReader->Read(&notification)) {
                    if (notification.has_pending_firewall_update()) {
                        aos::networkmanager::PendingFirewallUpdate update;

                        if (auto err
                            = common::pbconvert::ConvertFromProto(notification.pending_firewall_update(), update);
                            !err.IsNone()) {
                            LOG_ERR() << "Failed to convert pending firewall update" << Log::Field(err);
                            continue;
                        }

                        mNetworkUpdateHandler->OnPendingFirewallUpdate(mNodeID.c_str(), update);
                    }
                }

                std::lock_guard lock {mMutex};

                auto status = mNetworkUpdateReader->Finish();
                if (!status.ok() && !mStopped) {
                    LOG_WRN() << "Network update stream disconnected"
                              << Log::Field("error", status.error_message().c_str());
                }

                mNetworkUpdateReader.reset();
                mNetworkUpdateCtx.reset();
            } else {
                LOG_ERR() << "Failed to subscribe to instance network updates";
            }

            {
                std::unique_lock lock {mMutex};

                mNetworkUpdateCV.wait_for(lock, std::chrono::nanoseconds(mConfig.mCMReconnectTimeout.Nanoseconds()),
                    [this] { return mStopped; });

                if (mStopped) {
                    break;
                }
            }
        }

        LOG_DBG() << "Network update subscription thread stopped";
    });
}

} // namespace aos::sm::smclient
