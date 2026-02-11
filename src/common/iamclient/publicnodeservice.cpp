/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <grpcpp/grpcpp.h>

#include <core/common/tools/logger.hpp>

#include <common/pbconvert/common.hpp>
#include <common/utils/exception.hpp>

#include "publicnodeservice.hpp"

namespace aos::common::iamclient {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

PublicNodesService::~PublicNodesService()
{
    Stop();

    if (mSubscriptionManager) {
        mSubscriptionManager->Close();
    }
}

Error PublicNodesService::Init(const std::string& iamServerURL, TLSCredentialsItf& tlsCredentials,
    bool insecureConnection, bool publicServer, const std::string& certStorage)
{
    LOG_DBG() << "Init public nodes service" << Log::Field("iamServerURL", iamServerURL.c_str())
              << Log::Field("publicServer", publicServer) << Log::Field("insecureConnection", insecureConnection);

    std::lock_guard lock {mMutex};

    mTLSCredentials     = &tlsCredentials;
    mIAMPublicServerURL = iamServerURL;
    mInsecureConnection = insecureConnection;
    mPublicServer       = publicServer;
    mCertStorage        = certStorage;

    Error err;

    Tie(mCredentials, err) = CreateCredential();
    if (!err.IsNone()) {
        return err;
    }

    mStub = iamanager::v6::IAMPublicNodesService::NewStub(
        grpc::CreateCustomChannel(mIAMPublicServerURL, mCredentials, grpc::ChannelArguments()));

    return ErrorEnum::eNone;
}

Error PublicNodesService::Reconnect()
{
    std::lock_guard lock {mMutex};

    LOG_INF() << "Reconnect public nodes service";

    Error err;

    Tie(mCredentials, err) = CreateCredential();
    if (!err.IsNone()) {
        return err;
    }

    mStub = iamanager::v6::IAMPublicNodesService::NewStub(
        grpc::CreateCustomChannel(mIAMPublicServerURL, mCredentials, grpc::ChannelArguments()));

    if (mSubscriptionManager) {
        mSubscriptionManager->Reconnect(mStub.get());
    }

    return ErrorEnum::eNone;
}

Error PublicNodesService::GetAllNodeIDs(Array<StaticString<cIDLen>>& ids) const
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Get all node IDs";

    auto ctx = std::make_unique<grpc::ClientContext>();
    ctx->set_deadline(std::chrono::system_clock::now() + cServiceTimeout);

    google::protobuf::Empty request;
    iamanager::v6::NodesID  response;

    if (auto status = mStub->GetAllNodeIDs(ctx.get(), request, &response); !status.ok()) {
        return Error(ErrorEnum::eRuntime, status.error_message().c_str());
    }

    for (const auto& nodeID : response.ids()) {
        if (auto err = ids.EmplaceBack(nodeID.c_str()); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    LOG_DBG() << "Node IDs received" << Log::Field("count", ids.Size());

    return ErrorEnum::eNone;
}

Error PublicNodesService::GetNodeInfo(const String& nodeID, NodeInfo& nodeInfo) const
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Get node info" << Log::Field("nodeID", nodeID);

    auto ctx = std::make_unique<grpc::ClientContext>();
    ctx->set_deadline(std::chrono::system_clock::now() + cServiceTimeout);

    iamanager::v6::GetNodeInfoRequest request;
    iamanager::v6::NodeInfo           response;

    request.set_node_id(nodeID.CStr());

    if (auto status = mStub->GetNodeInfo(ctx.get(), request, &response); !status.ok()) {
        return Error(ErrorEnum::eRuntime, status.error_message().c_str());
    }

    auto err = pbconvert::ConvertToAos(response, nodeInfo);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    LOG_DBG() << "Node info received" << Log::Field("nodeID", nodeInfo.mNodeID)
              << Log::Field("nodeType", nodeInfo.mNodeType);

    return ErrorEnum::eNone;
}

Error PublicNodesService::SubscribeListener(aos::iamclient::NodeInfoListenerItf& listener)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Subscribe to node info changed";

    if (!mSubscriptionManager) {
        google::protobuf::Empty request;

        auto convertFunc = [](const iamanager::v6::NodeInfo& proto, NodeInfo& aos) -> Error {
            return pbconvert::ConvertToAos(proto, aos);
        };

        auto notifyFunc = [](aos::iamclient::NodeInfoListenerItf& listener, const NodeInfo& nodeInfo) {
            listener.OnNodeInfoChanged(nodeInfo);
        };

        mSubscriptionManager = std::make_unique<NodeInfoSubscriptionManager>(mStub.get(), request,
            &iamanager::v6::IAMPublicNodesService::Stub::SubscribeNodeChanged, convertFunc, notifyFunc,
            "NodeSubscription");
    }

    return mSubscriptionManager->Subscribe(listener);
}

Error PublicNodesService::UnsubscribeListener(aos::iamclient::NodeInfoListenerItf& listener)
{
    std::lock_guard lock {mMutex};

    if (!mSubscriptionManager) {
        return ErrorEnum::eNone;
    }

    LOG_DBG() << "Unsubscribe from node info changed";

    if (mSubscriptionManager->Unsubscribe(listener)) {
        mSubscriptionManager.reset();
    }

    return ErrorEnum::eNone;
}

Error PublicNodesService::Start()
{
    std::lock_guard lock {mMutex};

    LOG_INF() << "Start node registration";

    if (mStart) {
        return ErrorEnum::eNone;
    }

    mStart            = true;
    mStop             = false;
    mConnectionThread = std::thread(&PublicNodesService::ConnectionLoop, this);

    return ErrorEnum::eNone;
}

void PublicNodesService::Stop()
{
    {
        std::lock_guard lock {mMutex};

        LOG_INF() << "Stop node registration";

        if (!mStart) {
            return;
        }

        mStop  = true;
        mStart = false;

        if (mRegisterNodeCtx) {
            mRegisterNodeCtx->TryCancel();
        }
    }

    mCV.notify_all();

    if (mConnectionThread.joinable()) {
        mConnectionThread.join();
    }
}

Error PublicNodesService::SendMessage(const iamanager::v6::IAMOutgoingMessages& message)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Send message";

    if (!mStream || !mConnected || mStop) {
        return Error(ErrorEnum::eCanceled, "stream is not connected");
    }

    if (!mStream->Write(message)) {
        return Error(ErrorEnum::eRuntime, "failed to write message");
    }

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

void PublicNodesService::ConnectionLoop()
{
    LOG_DBG() << "Connection loop started";

    while (!mStop) {
        if (auto err = RegisterNode(); !err.IsNone()) {
            LOG_ERR() << "Failed to register node" << Log::Field(err);
        }

        std::unique_lock lock {mMutex};

        LOG_WRN() << "Connection failed" << Log::Field("retryingInSec", cReconnectInterval.count());

        mCV.wait_for(lock, cReconnectInterval, [this]() { return mStop.load(); });
    }

    LOG_DBG() << "Connection loop stopped";
}

Error PublicNodesService::RegisterNode()
{
    {
        std::lock_guard lock {mMutex};

        LOG_DBG() << "Registering node";

        if (mStop) {
            return ErrorEnum::eNone;
        }

        mRegisterNodeCtx = std::make_unique<grpc::ClientContext>();

        if (mStream = mStub->RegisterNode(mRegisterNodeCtx.get()); !mStream) {
            return Error(ErrorEnum::eRuntime, "failed to create stream");
        }

        mConnected = true;

        LOG_INF() << "Node registration stream established";
    }

    OnConnected();

    HandleIncomingMessage();

    {
        std::lock_guard lock {mMutex};

        mConnected = false;
    }

    OnDisconnected();

    return ErrorEnum::eNone;
}

Error PublicNodesService::ReceiveMessage([[maybe_unused]] const iamanager::v6::IAMIncomingMessages&
        msg) // virtual function should be override in inherit classes
{
    return ErrorEnum::eNotSupported;
}

void PublicNodesService::OnConnected()
{
}

void PublicNodesService::OnDisconnected()
{
}

Error PublicNodesService::HandleIncomingMessage()
{
    iamanager::v6::IAMIncomingMessages incomingMsg;

    while (true) {
        if (!mStream->Read(&incomingMsg)) {
            LOG_WRN() << "Failed to read message or stream closed";

            return ErrorEnum::eFailed;
        }

        if (auto err = ReceiveMessage(incomingMsg); !err.IsNone()) {
            return err;
        }
    }
}

RetWithError<std::shared_ptr<grpc::ChannelCredentials>> PublicNodesService::CreateCredential()
{
    if (mInsecureConnection) {
        return grpc::InsecureChannelCredentials();
    }

    if (mPublicServer) {
        return mTLSCredentials->GetTLSClientCredentials();
    }

    return mTLSCredentials->GetMTLSClientCredentials(mCertStorage.c_str());
}

} // namespace aos::common::iamclient
