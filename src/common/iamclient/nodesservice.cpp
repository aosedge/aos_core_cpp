/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <grpcpp/grpcpp.h>

#include <common/logger/logmodule.hpp>
#include <common/pbconvert/common.hpp>
#include <common/utils/exception.hpp>
#include <iamanager/v6/iamanager.grpc.pb.h>

#include "nodesservice.hpp"

namespace aos::common::iamclient {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error NodesService::Init(const std::string& iamProtectedServerURL, const std::string& certStorage,
    TLSCredentialsItf& tlsCredentials, bool insecureConnection)
{
    LOG_DBG() << "Init nodes service" << Log::Field("iamProtectedServerURL", iamProtectedServerURL.c_str())
              << Log::Field("certStorage", certStorage.c_str()) << Log::Field("insecureConnection", insecureConnection);

    std::lock_guard lock {mMutex};

    mTLSCredentials        = &tlsCredentials;
    mIAMProtectedServerURL = iamProtectedServerURL;
    mCertStorage           = certStorage;
    mInsecureConnection    = insecureConnection;

    auto [credentials, err] = mTLSCredentials->GetMTLSClientCredentials(mCertStorage.c_str(), mInsecureConnection);
    if (!err.IsNone()) {
        return err;
    }

    mCredentials = credentials;

    mStub = iamanager::v6::IAMNodesService::NewStub(
        grpc::CreateCustomChannel(mIAMProtectedServerURL, mCredentials, grpc::ChannelArguments()));

    return ErrorEnum::eNone;
}

Error NodesService::Reconnect()
{
    std::lock_guard lock {mMutex};

    LOG_INF() << "Reconnect nodes service";

    auto [credentials, err] = mTLSCredentials->GetMTLSClientCredentials(mCertStorage.c_str(), mInsecureConnection);
    if (!err.IsNone()) {
        return err;
    }

    mCredentials = credentials;

    mStub = iamanager::v6::IAMNodesService::NewStub(
        grpc::CreateCustomChannel(mIAMProtectedServerURL, mCredentials, grpc::ChannelArguments()));

    return ErrorEnum::eNone;
}

Error NodesService::PauseNode(const String& nodeID)
{
    std::lock_guard lock {mMutex};

    LOG_INF() << "Pause node" << Log::Field("nodeID", nodeID);

    try {
        auto ctx = std::make_unique<grpc::ClientContext>();
        ctx->set_deadline(std::chrono::system_clock::now() + cServiceTimeout);

        iamanager::v6::PauseNodeRequest  request;
        iamanager::v6::PauseNodeResponse response;

        request.set_node_id(nodeID.CStr());

        if (auto status = mStub->PauseNode(ctx.get(), request, &response); !status.ok()) {
            return Error(ErrorEnum::eRuntime, status.error_message().c_str());
        }

        if (response.has_error()) {
            return Error(response.error().exit_code(), response.error().message().c_str());
        }

        return ErrorEnum::eNone;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(utils::ToAosError(e, ErrorEnum::eRuntime));
    }
}

Error NodesService::ResumeNode(const String& nodeID)
{
    std::lock_guard lock {mMutex};

    LOG_INF() << "Resume node" << Log::Field("nodeID", nodeID);

    try {
        auto ctx = std::make_unique<grpc::ClientContext>();
        ctx->set_deadline(std::chrono::system_clock::now() + cServiceTimeout);

        iamanager::v6::ResumeNodeRequest  request;
        iamanager::v6::ResumeNodeResponse response;

        request.set_node_id(nodeID.CStr());

        if (auto status = mStub->ResumeNode(ctx.get(), request, &response); !status.ok()) {
            return Error(ErrorEnum::eRuntime, status.error_message().c_str());
        }

        if (response.has_error()) {
            return Error(response.error().exit_code(), response.error().message().c_str());
        }

        return ErrorEnum::eNone;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(utils::ToAosError(e, ErrorEnum::eRuntime));
    }
}

} // namespace aos::common::iamclient
