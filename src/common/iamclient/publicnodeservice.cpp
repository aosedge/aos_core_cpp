/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <grpcpp/grpcpp.h>

#include <common/logger/logmodule.hpp>
#include <common/pbconvert/common.hpp>
#include <common/utils/exception.hpp>

#include "publicnodeservice.hpp"

namespace aos::common::iamclient {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

PublicNodesService::~PublicNodesService()
{
    if (mSubscriptionManager) {
        mSubscriptionManager->Close();
    }
}

Error PublicNodesService::Init(
    const std::string& iamPublicServerURL, TLSCredentialsItf& tlsCredentials, bool insecureConnection)
{
    LOG_INF() << "Init public nodes service" << Log::Field("iamPublicServerURL", iamPublicServerURL.c_str())
              << Log::Field("insecureConnection", insecureConnection);

    std::lock_guard lock {mMutex};

    mTLSCredentials     = &tlsCredentials;
    mIAMPublicServerURL = iamPublicServerURL;
    mInsecureConnection = insecureConnection;

    auto [credentials, err] = mTLSCredentials->GetTLSClientCredentials(mInsecureConnection);
    if (!err.IsNone()) {
        return err;
    }

    mCredentials = credentials;

    mStub = iamanager::v6::IAMPublicNodesService::NewStub(
        grpc::CreateCustomChannel(mIAMPublicServerURL, mCredentials, grpc::ChannelArguments()));

    return ErrorEnum::eNone;
}

Error PublicNodesService::Reconnect()
{
    std::lock_guard lock {mMutex};

    LOG_INF() << "Reconnect public nodes service";

    auto [credentials, err] = mTLSCredentials->GetTLSClientCredentials(mInsecureConnection);
    if (!err.IsNone()) {
        return err;
    }

    mCredentials = credentials;

    mStub = iamanager::v6::IAMPublicNodesService::NewStub(
        grpc::CreateCustomChannel(mIAMPublicServerURL, mCredentials, grpc::ChannelArguments()));

    if (mSubscriptionManager) {
        mSubscriptionManager->Reconnect(mStub.get());
    }

    return ErrorEnum::eNone;
}

Error PublicNodesService::GetAllNodeIds(Array<StaticString<cIDLen>>& ids) const
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

    LOG_INF() << "Subscribe to node info changed";

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

    LOG_INF() << "Unsubscribe from node info changed";

    if (mSubscriptionManager->Unsubscribe(listener)) {
        mSubscriptionManager.reset();
    }

    return ErrorEnum::eNone;
}

} // namespace aos::common::iamclient
