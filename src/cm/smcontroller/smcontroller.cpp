/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <vector>

#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server_builder.h>

#include <core/common/tools/logger.hpp>

#include <common/pbconvert/common.hpp>
#include <common/pbconvert/sm.hpp>
#include <common/utils/exception.hpp>
#include <common/utils/grpchelper.hpp>

#include "smcontroller.hpp"

namespace aos::cm::smcontroller {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error SMController::Init(const Config& config, cloudconnection::CloudConnectionItf& cloudConnection,
    iamclient::CertProviderItf& certProvider, crypto::CertLoaderItf& certLoader,
    crypto::x509::ProviderItf& cryptoProvider, imagemanager::ItemInfoProviderItf& itemInfoProvider,
    alerts::ReceiverItf& alertsReceiver, SenderItf& logSender, launcher::SenderItf& envVarsStatusSender,
    monitoring::ReceiverItf& monitoringReceiver, launcher::InstanceStatusReceiverItf& instanceStatusReceiver,
    nodeinfoprovider::SMInfoReceiverItf& smInfoReceiver, aos::networkmanager::NetworkProviderItf& networkProvider,
    bool insecureConn)
{
    LOG_DBG() << "Init SM controller";

    mConfig                 = config;
    mCloudConnection        = &cloudConnection;
    mCertProvider           = &certProvider;
    mCertLoader             = &certLoader;
    mCryptoProvider         = &cryptoProvider;
    mItemInfoProvider       = &itemInfoProvider;
    mAlertsReceiver         = &alertsReceiver;
    mLogSender              = &logSender;
    mEnvVarsStatusSender    = &envVarsStatusSender;
    mMonitoringReceiver     = &monitoringReceiver;
    mInstanceStatusReceiver = &instanceStatusReceiver;
    mSMInfoReceiver         = &smInfoReceiver;
    mNetworkProvider        = &networkProvider;
    mInsecureConn           = insecureConn;

    return ErrorEnum::eNone;
}

Error SMController::Start()
{
    LOG_DBG() << "Start SM Controller";

    if (auto err = CreateServerCredentials(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = StartServer(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = mCertProvider->SubscribeListener(String(mConfig.mCertStorage.c_str()), *this); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = mCloudConnection->SubscribeListener(*this); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error SMController::Stop()
{
    LOG_DBG() << "Stop SM Controller";

    if (auto err = mReconnectTimer.Stop(); !err.IsNone() && !err.Is(ErrorEnum::eWrongState)) {
        LOG_ERR() << "Failed to stop reconnect timer" << Log::Field(err);
    }

    if (auto err = StopServer(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = mCertProvider->UnsubscribeListener(*this); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = mCloudConnection->UnsubscribeListener(*this); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * NodeConfigHandlerItf implementation
 **********************************************************************************************************************/

Error SMController::CheckNodeConfig(const String& nodeID, const NodeConfig& config)
{
    LOG_DBG() << "Checking node config" << Log::Field("nodeID", nodeID);

    SMHandler* handler = FindNode(nodeID);
    if (!handler) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eNotFound, "node not found"));
    }

    if (auto err = handler->CheckNodeConfig(config); !err.IsNone()) {
        return err;
    }

    return ErrorEnum::eNone;
}

Error SMController::UpdateNodeConfig(const String& nodeID, const NodeConfig& config)
{
    LOG_DBG() << "Updating config" << Log::Field("nodeID", nodeID);

    SMHandler* handler = FindNode(nodeID);
    if (!handler) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eNotFound, "node not found"));
    }

    if (auto err = handler->UpdateNodeConfig(config); !err.IsNone()) {
        return err;
    }

    return ErrorEnum::eNone;
}

Error SMController::GetNodeConfigStatus(const String& nodeID, NodeConfigStatus& status)
{
    LOG_DBG() << "Getting config status" << Log::Field("nodeID", nodeID);

    SMHandler* handler = FindNode(nodeID);
    if (!handler) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eNotFound, "node not found"));
    }

    if (auto err = handler->GetNodeConfigStatus(status); !err.IsNone()) {
        return err;
    }

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * LogProviderItf implementation
 **********************************************************************************************************************/

Error SMController::RequestLog(const aos::RequestLog& log)
{
    LOG_DBG() << "Requesting log" << Log::Field("correlationId", log.mCorrelationID);

    for (const auto& nodeID : log.mFilter.mNodes) {
        SMHandler* handler = FindNode(nodeID);
        if (!handler) {
            return AOS_ERROR_WRAP(Error(ErrorEnum::eNotFound, "node not found"));
        }

        if (auto err = handler->RequestLog(log); !err.IsNone()) {
            return err;
        }
    }

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * InstanceRunnerItf implementation
 **********************************************************************************************************************/

Error SMController::UpdateInstances(
    const String& nodeID, const Array<aos::InstanceInfo>& stopInstances, const Array<aos::InstanceInfo>& startInstances)
{
    LOG_DBG() << "Updating instances" << Log::Field("nodeID", nodeID.CStr());

    SMHandler* handler = FindNode(nodeID);
    if (!handler) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eNotFound, "node not found"));
    }

    if (auto err = handler->UpdateInstances(stopInstances, startInstances); !err.IsNone()) {
        return err;
    }

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * MonitoringProviderItf implementation
 **********************************************************************************************************************/

Error SMController::GetAverageMonitoring(const String& nodeID, aos::monitoring::NodeMonitoringData& monitoring)
{
    LOG_DBG() << "Getting average monitoring" << Log::Field("nodeID", nodeID.CStr());

    SMHandler* handler = FindNode(nodeID);
    if (!handler) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eNotFound, "node not found"));
    }

    if (auto err = handler->GetAverageMonitoring(monitoring); !err.IsNone()) {
        return err;
    }

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * ConnectionListenerItf implementation
 **********************************************************************************************************************/

void SMController::OnConnect()
{
    LOG_DBG() << "Cloud connected";

    std::lock_guard lock {mMutex};

    for (auto& handler : mSMHandlers) {
        handler->OnConnect();
    }
}

void SMController::OnDisconnect()
{
    LOG_DBG() << "Cloud disconnected";

    std::lock_guard lock {mMutex};

    for (auto& handler : mSMHandlers) {
        handler->OnDisconnect();
    }
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

grpc::Status SMController::RegisterSM(grpc::ServerContext*                                                    context,
    grpc::ServerReaderWriter<servicemanager::v5::SMIncomingMessages, servicemanager::v5::SMOutgoingMessages>* stream)
{
    LOG_INF() << "SM registration request received";

    auto handler = std::make_shared<SMHandler>(context, stream, *mAlertsReceiver, *mLogSender, *mEnvVarsStatusSender,
        *mMonitoringReceiver, *mInstanceStatusReceiver, *mSMInfoReceiver,
        static_cast<NodeConnectionStatusListenerItf&>(*this));

    {
        std::lock_guard lock {mMutex};

        handler->Start();

        handler->SendCloudConnectionStatus(mCloudConnection->IsConnected());

        mSMHandlers.push_back(handler);
    }

    handler->Wait();

    {
        std::lock_guard lock {mMutex};

        mSMHandlers.erase(std::remove(mSMHandlers.begin(), mSMHandlers.end(), handler), mSMHandlers.end());

        mAllNodesDisconnectedCV.notify_one();
    }

    return grpc::Status::OK;
}

grpc::Status SMController::GetBlobsInfos(grpc::ServerContext*, const servicemanager::v5::BlobsInfosRequest* request,
    servicemanager::v5::BlobsInfos* response)
{
    LOG_DBG() << "Get blobs info request received" << Log::Field("digestsCount", request->digests_size());

    for (int i = 0; i < request->digests_size(); ++i) {
        StaticString<cURLLen> digestURL;

        if (auto err = mItemInfoProvider->GetBlobURL(request->digests(i).c_str(), digestURL); !err.IsNone()) {
            return grpc::Status(grpc::StatusCode::INTERNAL, err.Message());
        }

        response->add_urls(digestURL.CStr());
    }

    return grpc::Status::OK;
}

grpc::Status SMController::GetNodeNetworkParams(grpc::ServerContext*,
    const servicemanager::v5::GetNodeNetworkParamsRequest* request,
    servicemanager::v5::GetNodeNetworkParamsResponse*      response)
{
    LOG_DBG() << "GetNodeNetworkParams" << Log::Field("networkID", request->network_id().c_str())
              << Log::Field("nodeID", request->node_id().c_str());

    NetworkParams result;

    if (auto err
        = mNetworkProvider->GetNodeNetworkParams(request->network_id().c_str(), request->node_id().c_str(), result);
        !err.IsNone()) {
        common::pbconvert::SetErrorInfo(err, *response);

        return grpc::Status::OK;
    }

    if (auto err = common::pbconvert::ConvertToProto(result, *response); !err.IsNone()) {
        return grpc::Status(grpc::StatusCode::INTERNAL, err.Message());
    }

    return grpc::Status::OK;
}

grpc::Status SMController::AllocateInstanceNetwork(grpc::ServerContext*,
    const servicemanager::v5::AllocateInstanceNetworkRequest* request,
    servicemanager::v5::AllocateInstanceNetworkResponse*      response)
{
    LOG_DBG() << "AllocateInstanceNetwork" << Log::Field("networkID", request->network_id().c_str())
              << Log::Field("nodeID", request->node_id().c_str());

    auto instance = common::pbconvert::ConvertToAos(request->instance());

    auto serviceData = std::make_unique<UpdateItemNetworkParams>();

    if (auto err = common::pbconvert::ConvertFromProto(request->service_data(), *serviceData); !err.IsNone()) {
        return grpc::Status(grpc::StatusCode::INTERNAL, err.Message());
    }

    auto result = std::make_unique<InstanceNetworkAllocation>();

    if (auto err = mNetworkProvider->AllocateInstanceNetwork(
            instance, request->network_id().c_str(), request->node_id().c_str(), *serviceData, *result);
        !err.IsNone()) {
        common::pbconvert::SetErrorInfo(err, *response);

        return grpc::Status::OK;
    }

    if (auto err = common::pbconvert::ConvertToProto(*result, *response); !err.IsNone()) {
        return grpc::Status(grpc::StatusCode::INTERNAL, err.Message());
    }

    return grpc::Status::OK;
}

grpc::Status SMController::ReleaseInstanceNetwork(grpc::ServerContext*,
    const servicemanager::v5::ReleaseInstanceNetworkRequest* request,
    servicemanager::v5::ReleaseInstanceNetworkResponse*      response)
{
    LOG_DBG() << "ReleaseInstanceNetwork" << Log::Field("nodeID", request->node_id().c_str());

    auto instance = common::pbconvert::ConvertToAos(request->instance());

    if (auto err = mNetworkProvider->ReleaseInstanceNetwork(instance, request->node_id().c_str()); !err.IsNone()) {
        common::pbconvert::SetErrorInfo(err, *response);
    }

    return grpc::Status::OK;
}

grpc::Status SMController::ReleaseNodeNetwork(grpc::ServerContext*,
    const servicemanager::v5::ReleaseNodeNetworkRequest* request,
    servicemanager::v5::ReleaseNodeNetworkResponse*      response)
{
    LOG_DBG() << "ReleaseNodeNetwork" << Log::Field("networkID", request->network_id().c_str())
              << Log::Field("nodeID", request->node_id().c_str());

    if (auto err = mNetworkProvider->ReleaseNodeNetwork(request->network_id().c_str(), request->node_id().c_str());
        !err.IsNone()) {
        common::pbconvert::SetErrorInfo(err, *response);
    }

    return grpc::Status::OK;
}

void SMController::OnCertChanged(const CertInfo& info)
{
    (void)info;

    LOG_INF() << "Certificate changed, restart SM controller";

    ScheduleRestart();
}

void SMController::OnNodeConnected(const String& nodeID)
{
    LOG_INF() << "SM client connected" << Log::Field("nodeID", nodeID);

    mSMInfoReceiver->OnSMConnected(nodeID);
}

void SMController::OnNodeDisconnected(const String& nodeID)
{
    LOG_INF() << "SM client disconnected" << Log::Field("nodeID", nodeID);

    {
        std::lock_guard lock {mMutex};

        auto it = std::find_if(mSMHandlers.begin(), mSMHandlers.end(),
            [&nodeID](const std::shared_ptr<SMHandler>& handler) { return handler->GetNodeID() == nodeID; });

        if (it != mSMHandlers.end()) {
            mSMHandlers.erase(it);
        }
    }

    mSMInfoReceiver->OnSMDisconnected(nodeID, ErrorEnum::eNone);
}

SMHandler* SMController::FindNode(const String& nodeID)
{
    std::lock_guard lock {mMutex};

    auto it = std::find_if(mSMHandlers.begin(), mSMHandlers.end(),
        [&nodeID](const std::shared_ptr<SMHandler>& handler) { return handler->GetNodeID() == nodeID; });

    return (it != mSMHandlers.end()) ? it->get() : nullptr;
}

Error SMController::CreateServerCredentials()
{
    if (!mInsecureConn) {
        auto certInfo = std::make_unique<CertInfo>();

        if (auto err = mCertProvider->GetCert(String(mConfig.mCertStorage.c_str()), {}, {}, *certInfo); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        mCredentials = aos::common::utils::GetMTLSServerCredentials(
            *certInfo, mConfig.mCACert.c_str(), *mCertLoader, *mCryptoProvider);
    } else {
        mCredentials = grpc::InsecureServerCredentials();
    }

    return ErrorEnum::eNone;
}

RetWithError<std::string> SMController::CorrectAddress(const std::string& addr) const
{
    if (addr.empty()) {
        return {addr, AOS_ERROR_WRAP(ErrorEnum::eInvalidArgument)};
    }

    if (addr[0] == ':') {
        return "0.0.0.0" + addr;
    }

    return addr;
}

Error SMController::StartServer()
{
    auto [correctedAddress, err] = CorrectAddress(mConfig.mCMServerURL);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    grpc::ServerBuilder builder;

    common::utils::SetGRPCServerOptions(builder);

    builder.AddListeningPort(correctedAddress, mCredentials);
    builder.RegisterService(static_cast<servicemanager::v5::SMService::Service*>(this));
    builder.RegisterService(static_cast<servicemanager::v5::NetworkService::Service*>(this));

    mServer = builder.BuildAndStart();
    if (!mServer) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "failed to start CM server"));
    }

    LOG_INF() << "CM server started on" << Log::Field("url", correctedAddress.c_str());

    return ErrorEnum::eNone;
}

Error SMController::StopServer()
{
    {
        std::lock_guard lock {mMutex};

        for (auto& handler : mSMHandlers) {
            if (handler) {
                handler->Stop();
            }
        }

        for (auto& [_, stream] : mNetworkUpdateStreams) {
            stream.mContext->TryCancel();
        }

        mStreamCV.notify_all();
    }

    if (mServer) {
        mServer->Shutdown();
        mServer->Wait();

        mServer.reset();
    }

    {
        // waiting for the threads running RegisterSM to be stopped
        std::unique_lock lock {mMutex};

        mAllNodesDisconnectedCV.wait(lock, [this]() { return mSMHandlers.empty(); });
    }

    return ErrorEnum::eNone;
}

Error SMController::RestartServer()
{
    if (auto err = StopServer(); !err.IsNone()) {
        LOG_ERR() << "Failed to stop SM server" << Log::Field(err);

        return err;
    }

    if (auto err = CreateServerCredentials(); !err.IsNone()) {
        LOG_ERR() << "Failed to create SM server credentials" << Log::Field(err);

        return err;
    }

    if (auto err = StartServer(); !err.IsNone()) {
        LOG_ERR() << "Failed to start SM server" << Log::Field(err);

        return err;
    }

    return ErrorEnum::eNone;
}

void SMController::ScheduleRestart()
{
    if (auto err = mReconnectTimer.Stop(); !err.IsNone() && !err.Is(ErrorEnum::eWrongState)) {
        LOG_ERR() << "Failed to stop reconnect timer" << Log::Field(err);
    }

    if (auto err = mReconnectTimer.Start(
            cReconnectRetryTimeout, [this](void*) { OnRestartTimer(); }, false);
        !err.IsNone()) {
        LOG_ERR() << "Failed to start reconnect timer" << Log::Field(err);
    }
}

void SMController::OnRestartTimer()
{
    auto err = RestartServer();
    if (!err.IsNone()) {
        LOG_ERR() << "SM controller restart failed, retrying" << Log::Field(err);

        return;
    }

    LOG_INF() << "SM controller restarted successfully";

    if (auto stopErr = mReconnectTimer.Stop(); !stopErr.IsNone() && !stopErr.Is(ErrorEnum::eWrongState)) {
        LOG_ERR() << "Failed to stop reconnect timer" << Log::Field(stopErr);
    }
}

grpc::Status SMController::SubscribeInstanceNetworkUpdates(grpc::ServerContext* context,
    const servicemanager::v5::SubscribeInstanceNetworkUpdatesRequest*           request,
    grpc::ServerWriter<servicemanager::v5::InstanceNetworkUpdateNotification>*  writer)
{
    auto nodeID = request->node_id();

    LOG_DBG() << "SubscribeInstanceNetworkUpdates" << Log::Field("nodeID", nodeID.c_str());

    {
        std::lock_guard lock {mStreamMutex};
        mNetworkUpdateStreams[nodeID] = {context, writer};
    }

    {
        std::unique_lock lock {mStreamMutex};

        while (!context->IsCancelled()) {
            mStreamCV.wait_for(lock, cStreamPollInterval);
        }
    }

    {
        std::lock_guard lock {mStreamMutex};

        // Only erase if this is still the active stream (not replaced by a newer subscription)
        auto it = mNetworkUpdateStreams.find(nodeID);
        if (it != mNetworkUpdateStreams.end() && it->second.mWriter == writer) {
            mNetworkUpdateStreams.erase(it);
        }
    }

    LOG_DBG() << "SubscribeInstanceNetworkUpdates disconnected" << Log::Field("nodeID", nodeID.c_str());

    return grpc::Status::OK;
}

grpc::Status SMController::SyncNetworkState(grpc::ServerContext*,
    const servicemanager::v5::SyncNetworkStateRequest* request, servicemanager::v5::SyncNetworkStateResponse* response)
{
    LOG_DBG() << "Sync network state" << Log::Field("nodeID", request->node_id().c_str())
              << Log::Field("instancesCount", request->instances_size());

    auto instances = std::make_unique<StaticArray<InstanceNetworkStateInfo, cMaxNumInstances>>();

    for (const auto& protoInstance : request->instances()) {
        InstanceNetworkStateInfo info;

        if (auto err = common::pbconvert::ConvertFromProto(protoInstance, info); !err.IsNone()) {
            common::pbconvert::SetErrorInfo(err, *response);

            return grpc::Status::OK;
        }

        if (auto err = instances->PushBack(info); !err.IsNone()) {
            common::pbconvert::SetErrorInfo(err, *response);

            return grpc::Status::OK;
        }
    }

    if (auto err = mNetworkProvider->SyncNetworkState(request->node_id().c_str(), *instances); !err.IsNone()) {
        common::pbconvert::SetErrorInfo(err, *response);
    }

    return grpc::Status::OK;
}

void SMController::OnPendingFirewallUpdate(
    const String& nodeID, const aos::networkmanager::PendingFirewallUpdate& update)
{
    std::lock_guard lock {mStreamMutex};

    auto it = mNetworkUpdateStreams.find(nodeID.CStr());
    if (it == mNetworkUpdateStreams.end()) {
        LOG_WRN() << "No stream writer for node" << Log::Field("nodeID", nodeID);
        return;
    }

    servicemanager::v5::InstanceNetworkUpdateNotification notification;
    auto* pendingUpdate = notification.mutable_pending_firewall_update();

    if (auto err = common::pbconvert::ConvertToProto(update, *pendingUpdate); !err.IsNone()) {
        LOG_ERR() << "Failed to convert pending firewall update" << Log::Field(err);
        return;
    }

    if (!it->second.mWriter->Write(notification)) {
        LOG_ERR() << "Failed to write pending firewall update to stream" << Log::Field("nodeID", nodeID);
    }
}

} // namespace aos::cm::smcontroller
