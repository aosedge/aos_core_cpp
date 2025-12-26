/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <grpcpp/grpcpp.h>

#include <core/common/tools/logger.hpp>

#include <common/pbconvert/common.hpp>
#include <common/utils/exception.hpp>

#include "publiccurrentnodeservice.hpp"

namespace aos::common::iamclient {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

PublicCurrentNodeService::~PublicCurrentNodeService()
{
    if (mSubscriptionManager) {
        mSubscriptionManager->Close();
    }
}

Error PublicCurrentNodeService::Init(
    const std::string& iamPublicServerURL, TLSCredentialsItf& tlsCredentials, bool insecureConnection)
{
    LOG_DBG() << "Init public current node service" << Log::Field("iamPublicServerURL", iamPublicServerURL.c_str())
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

    mStub = iamanager::v6::IAMPublicCurrentNodeService::NewStub(
        grpc::CreateCustomChannel(mIAMPublicServerURL, mCredentials, grpc::ChannelArguments()));

    return ErrorEnum::eNone;
}

Error PublicCurrentNodeService::Reconnect()
{
    std::lock_guard lock {mMutex};

    LOG_INF() << "Reconnect public current node service";

    auto [credentials, err] = mTLSCredentials->GetTLSClientCredentials(mInsecureConnection);
    if (!err.IsNone()) {
        return err;
    }

    mCredentials = credentials;

    mStub = iamanager::v6::IAMPublicCurrentNodeService::NewStub(
        grpc::CreateCustomChannel(mIAMPublicServerURL, mCredentials, grpc::ChannelArguments()));

    if (mSubscriptionManager) {
        mSubscriptionManager->Reconnect(mStub.get());
    }

    return ErrorEnum::eNone;
}

Error PublicCurrentNodeService::GetCurrentNodeInfo(NodeInfo& nodeInfo) const
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Get current node info";

    auto ctx = std::make_unique<grpc::ClientContext>();
    ctx->set_deadline(std::chrono::system_clock::now() + cServiceTimeout);

    google::protobuf::Empty request;
    iamanager::v6::NodeInfo response;

    if (auto status = mStub->GetCurrentNodeInfo(ctx.get(), request, &response); !status.ok()) {
        return Error(ErrorEnum::eRuntime, status.error_message().c_str());
    }

    auto err = pbconvert::ConvertToAos(response, nodeInfo);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    LOG_DBG() << "Current node info received" << Log::Field("nodeID", nodeInfo.mNodeID)
              << Log::Field("nodeType", nodeInfo.mNodeType);

    return ErrorEnum::eNone;
}

Error PublicCurrentNodeService::SubscribeListener(aos::iamclient::CurrentNodeInfoListenerItf& listener)
{
    std::lock_guard lock {mMutex};

    LOG_INF() << "Subscribe to current node info changed";

    if (!mSubscriptionManager) {
        google::protobuf::Empty request;

        auto convertFunc = [](const iamanager::v6::NodeInfo& proto, NodeInfo& aos) -> Error {
            return pbconvert::ConvertToAos(proto, aos);
        };

        auto notifyFunc = [](aos::iamclient::CurrentNodeInfoListenerItf& listener, const NodeInfo& nodeInfo) {
            listener.OnCurrentNodeInfoChanged(nodeInfo);
        };

        mSubscriptionManager = std::make_unique<CurrentNodeInfoSubscriptionManager>(mStub.get(), request,
            &iamanager::v6::IAMPublicCurrentNodeService::Stub::SubscribeCurrentNodeChanged, convertFunc, notifyFunc,
            "CurrentNodeSubscription");
    }

    return mSubscriptionManager->Subscribe(listener);
}

Error PublicCurrentNodeService::UnsubscribeListener(aos::iamclient::CurrentNodeInfoListenerItf& listener)
{
    std::lock_guard lock {mMutex};

    if (!mSubscriptionManager) {
        return ErrorEnum::eNone;
    }

    LOG_INF() << "Unsubscribe from current node info changed";

    if (mSubscriptionManager->Unsubscribe(listener)) {
        mSubscriptionManager.reset();
    }

    return ErrorEnum::eNone;
}

} // namespace aos::common::iamclient
