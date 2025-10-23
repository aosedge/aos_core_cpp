/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <grpcpp/grpcpp.h>

#include <common/logger/logmodule.hpp>
#include <common/utils/exception.hpp>

#include "provisioningservice.hpp"

namespace aos::common::iamclient {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error ProvisioningService::Init(const std::string& iamProtectedServerURL, const std::string& certStorage,
    TLSCredentialsItf& tlsCredentials, bool insecureConnection)
{
    LOG_DBG() << "Init provisioning service" << Log::Field("iamProtectedServerURL", iamProtectedServerURL.c_str())
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

    mStub = iamanager::v6::IAMProvisioningService::NewStub(
        grpc::CreateCustomChannel(mIAMProtectedServerURL, mCredentials, grpc::ChannelArguments()));

    return ErrorEnum::eNone;
}

Error ProvisioningService::Reconnect()
{
    std::lock_guard lock {mMutex};

    LOG_INF() << "Reconnect provisioning service";

    auto [credentials, err] = mTLSCredentials->GetMTLSClientCredentials(mCertStorage.c_str(), mInsecureConnection);
    if (!err.IsNone()) {
        return err;
    }

    mCredentials = credentials;

    mStub = iamanager::v6::IAMProvisioningService::NewStub(
        grpc::CreateCustomChannel(mIAMProtectedServerURL, mCredentials, grpc::ChannelArguments()));

    return ErrorEnum::eNone;
}

Error ProvisioningService::GetCertTypes(const String& nodeID, Array<StaticString<cCertTypeLen>>& certTypes) const
{
    std::lock_guard lock {mMutex};

    LOG_INF() << "Get cert types" << Log::Field("nodeID", nodeID);

    try {
        auto ctx = std::make_unique<grpc::ClientContext>();
        ctx->set_deadline(std::chrono::system_clock::now() + cServiceTimeout);

        iamanager::v6::GetCertTypesRequest request;
        iamanager::v6::CertTypes           response;

        request.set_node_id(nodeID.CStr());

        if (auto status = mStub->GetCertTypes(ctx.get(), request, &response); !status.ok()) {
            return Error(ErrorEnum::eRuntime, status.error_message().c_str());
        }

        for (const auto& type : response.types()) {
            if (auto err = certTypes.EmplaceBack(type.c_str()); !err.IsNone()) {
                return AOS_ERROR_WRAP(err);
            }
        }

        return ErrorEnum::eNone;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(utils::ToAosError(e, ErrorEnum::eRuntime));
    }
}

Error ProvisioningService::StartProvisioning(const String& nodeID, const String& password)
{
    std::lock_guard lock {mMutex};

    LOG_INF() << "Start provisioning" << Log::Field("nodeID", nodeID);

    try {
        auto ctx = std::make_unique<grpc::ClientContext>();
        ctx->set_deadline(std::chrono::system_clock::now() + cServiceTimeout);

        iamanager::v6::StartProvisioningRequest  request;
        iamanager::v6::StartProvisioningResponse response;

        request.set_node_id(nodeID.CStr());
        request.set_password(password.CStr());

        if (auto status = mStub->StartProvisioning(ctx.get(), request, &response); !status.ok()) {
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

Error ProvisioningService::FinishProvisioning(const String& nodeID, const String& password)
{
    std::lock_guard lock {mMutex};

    LOG_INF() << "Finish provisioning" << Log::Field("nodeID", nodeID);

    try {
        auto ctx = std::make_unique<grpc::ClientContext>();
        ctx->set_deadline(std::chrono::system_clock::now() + cServiceTimeout);

        iamanager::v6::FinishProvisioningRequest  request;
        iamanager::v6::FinishProvisioningResponse response;

        request.set_node_id(nodeID.CStr());
        request.set_password(password.CStr());

        if (auto status = mStub->FinishProvisioning(ctx.get(), request, &response); !status.ok()) {
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

Error ProvisioningService::Deprovision(const String& nodeID, const String& password)
{
    std::lock_guard lock {mMutex};

    LOG_INF() << "Deprovision" << Log::Field("nodeID", nodeID);

    try {
        auto ctx = std::make_unique<grpc::ClientContext>();
        ctx->set_deadline(std::chrono::system_clock::now() + cServiceTimeout);

        iamanager::v6::DeprovisionRequest  request;
        iamanager::v6::DeprovisionResponse response;

        request.set_node_id(nodeID.CStr());
        request.set_password(password.CStr());

        if (auto status = mStub->Deprovision(ctx.get(), request, &response); !status.ok()) {
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
