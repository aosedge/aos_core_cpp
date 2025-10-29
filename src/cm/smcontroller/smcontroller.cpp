/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <vector>

#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server_builder.h>

#include <common/logger/logmodule.hpp>
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
    crypto::x509::ProviderItf& cryptoProvider, imagemanager::BlobInfoProviderItf& blobInfoProvider,
    alerts::ReceiverItf& alertsReceiver, SenderItf& logSender, launcher::SenderItf& envVarsStatusSender,
    monitoring::ReceiverItf& monitoringReceiver, launcher::InstanceStatusReceiverItf& instanceStatusReceiver,
    nodeinfoprovider::SMInfoReceiverItf& smInfoReceiver, bool insecureConn)
{
    LOG_INF() << "Initialize SM Controller";

    mConfig                 = config;
    mCloudConnection        = &cloudConnection;
    mCertProvider           = &certProvider;
    mCertLoader             = &certLoader;
    mCryptoProvider         = &cryptoProvider;
    mBlobInfoProvider       = &blobInfoProvider;
    mAlertsReceiver         = &alertsReceiver;
    mLogSender              = &logSender;
    mEnvVarsStatusSender    = &envVarsStatusSender;
    mMonitoringReceiver     = &monitoringReceiver;
    mInstanceStatusReceiver = &instanceStatusReceiver;
    mSMInfoReceiver         = &smInfoReceiver;
    mInsecureConn           = insecureConn;

    if (auto err = CreateServerCredentials(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error SMController::Start()
{
    LOG_INF() << "Start SM Controller";

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
    LOG_INF() << "Stop SM Controller";

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
    LOG_DBG() << "Requesting log" << Log::Field("correlationID", log.mCorrelationID);

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
 * NodeNetworkItf implementation
 **********************************************************************************************************************/

Error SMController::UpdateNetworks(const String& nodeID, const Array<UpdateNetworkParameters>& networkParameters)
{
    LOG_DBG() << "Updating networks" << Log::Field("nodeID", nodeID.CStr());

    SMHandler* handler = FindNode(nodeID);
    if (!handler) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eNotFound, "node not found"));
    }

    if (auto err = handler->UpdateNetworks(networkParameters); !err.IsNone()) {
        return err;
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
    LOG_INF() << "Cloud connected";

    std::lock_guard lock {mMutex};

    for (auto& handler : mSMHandlers) {
        handler->OnConnect();
    }
}

void SMController::OnDisconnect()
{
    LOG_INF() << "Cloud disconnected";

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

grpc::Status SMController::GetBlobsInfos(grpc::ServerContext* /*context*/,
    const servicemanager::v5::BlobsInfosRequest* request, servicemanager::v5::BlobsInfos* response)
{
    LOG_DBG() << "Get blobs info request received: digests count=" << request->digests_size();

    std::vector<StaticString<oci::cDigestLen>> digests;
    digests.reserve(request->digests_size());

    for (int i = 0; i < request->digests_size(); ++i) {
        digests.push_back(request->digests(i).c_str());
    }

    auto digestsArray = Array<StaticString<oci::cDigestLen>>(digests.data(), digests.size());

    std::vector<BlobInfo> blobsInfo(request->digests_size());
    auto                  blobsInfoArray = Array<BlobInfo>(blobsInfo.data(), blobsInfo.size());

    if (auto err = mBlobInfoProvider->GetBlobsInfos(digestsArray, blobsInfoArray); !err.IsNone()) {
        return grpc::Status(grpc::StatusCode::INTERNAL, err.Message());
    }

    if (blobsInfoArray.Size() != digestsArray.Size()) {
        return grpc::Status(grpc::StatusCode::NOT_FOUND, "some blobs info not found");
    }

    for (const auto& blobInfo : blobsInfo) {
        if (blobInfo.mURLs.Size() != 1) {
            return grpc::Status(grpc::StatusCode::NOT_FOUND, "blob URL not found");
        }

        response->add_urls(blobInfo.mURLs[0].CStr());
    }

    return grpc::Status::OK;
}

void SMController::OnCertChanged(const CertInfo& info)
{
    (void)info;

    LOG_DBG() << "Certificate changed";

    if (auto err = CreateServerCredentials(); !err.IsNone()) {
        LOG_ERR() << "Failed to create server credentials" << Log::Field(err);

        return;
    }

    if (auto err = Stop(); !err.IsNone()) {
        LOG_ERR() << "Failed to stop server" << Log::Field(err);

        return;
    }

    if (auto err = Start(); !err.IsNone()) {
        LOG_ERR() << "Failed to start server" << Log::Field(err);

        return;
    }
}

void SMController::OnNodeConnected(const String& nodeID)
{
    LOG_INF() << "SM client connected" << Log::Field("nodeID", nodeID);

    mSMInfoReceiver->OnSMConnected(nodeID);
}

void SMController::OnNodeDisconnected(const String& nodeID)
{
    LOG_INF() << "SM client disconnected" << Log::Field("nodeID", nodeID);

    std::lock_guard lock {mMutex};

    auto it = std::find_if(mSMHandlers.begin(), mSMHandlers.end(),
        [&nodeID](const std::shared_ptr<SMHandler>& handler) { return handler->GetNodeID() == nodeID; });

    if (it != mSMHandlers.end()) {
        mSMHandlers.erase(it);
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

    builder.AddListeningPort(correctedAddress, mCredentials);
    builder.RegisterService(this);

    mServer = builder.BuildAndStart();
    if (!mServer) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "failed to start CM server"));
    }

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

} // namespace aos::cm::smcontroller
