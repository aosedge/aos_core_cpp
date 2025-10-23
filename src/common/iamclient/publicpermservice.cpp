/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <grpcpp/grpcpp.h>

#include <iamanager/v6/iamanager.grpc.pb.h>

#include <common/logger/logmodule.hpp>
#include <common/pbconvert/iam.hpp>
#include <common/utils/exception.hpp>

#include "publicpermservice.hpp"

namespace aos::common::iamclient {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error PublicPermissionsService::Init(
    std::string& iamPublicServerURL, TLSCredentialsItf& tlsCredentials, bool insecureConnection)
{
    LOG_DBG() << "Init public permissions service" << Log::Field("IAMPublicServerURL", iamPublicServerURL.c_str())
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

    mStub = iamanager::v6::IAMPublicPermissionsService::NewStub(
        grpc::CreateCustomChannel(mIAMPublicServerURL, mCredentials, grpc::ChannelArguments()));

    return ErrorEnum::eNone;
}

Error PublicPermissionsService::Reconnect()
{
    std::lock_guard lock {mMutex};

    LOG_INF() << "Reconnect public permissions service";

    auto [credentials, err] = mTLSCredentials->GetTLSClientCredentials(mInsecureConnection);
    if (!err.IsNone()) {
        return err;
    }

    mCredentials = credentials;

    mStub = iamanager::v6::IAMPublicPermissionsService::NewStub(
        grpc::CreateCustomChannel(mIAMPublicServerURL, mCredentials, grpc::ChannelArguments()));

    return ErrorEnum::eNone;
}

Error PublicPermissionsService::GetPermissions(const String& secret, const String& funcServerID,
    InstanceIdent& instanceIdent, Array<FunctionPermissions>& servicePermissions)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Get permissions" << Log::Field("funcServerID", funcServerID) << Log::Field("secret", secret)
              << Log::Field("instanceIdent", instanceIdent);

    try {
        auto ctx = std::make_unique<grpc::ClientContext>();
        ctx->set_deadline(std::chrono::system_clock::now() + cServiceTimeout);

        auto request = pbconvert::ConvertToProto(secret, funcServerID);

        iamanager::v6::PermissionsResponse response;

        if (auto status = mStub->GetPermissions(ctx.get(), request, &response); !status.ok()) {
            return ErrorEnum::eRuntime;
        }

        return pbconvert::ConvertToAos(response, instanceIdent, servicePermissions);
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(utils::ToAosError(e, ErrorEnum::eRuntime));
    }
}

} // namespace aos::common::iamclient
