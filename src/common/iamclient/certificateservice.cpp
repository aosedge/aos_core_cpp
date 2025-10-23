/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <grpcpp/grpcpp.h>

#include <common/logger/logmodule.hpp>
#include <common/pbconvert/iam.hpp>
#include <common/utils/exception.hpp>

#include "certificateservice.hpp"

namespace aos::common::iamclient {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error CertificateService::Init(const std::string& iamProtectedServerURL, const std::string& certStorage,
    TLSCredentialsItf& tlsCredentials, bool insecureConnection)
{
    LOG_DBG() << "Init certificate service" << Log::Field("iamProtectedServerURL", iamProtectedServerURL.c_str())
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

    mStub = iamanager::v6::IAMCertificateService::NewStub(
        grpc::CreateCustomChannel(mIAMProtectedServerURL, mCredentials, grpc::ChannelArguments()));

    return ErrorEnum::eNone;
}

Error CertificateService::Reconnect()
{
    std::lock_guard lock {mMutex};

    LOG_INF() << "Reconnect certificate service";

    auto [credentials, err] = mTLSCredentials->GetMTLSClientCredentials(mCertStorage.c_str(), mInsecureConnection);
    if (!err.IsNone()) {
        return err;
    }

    mCredentials = credentials;

    mStub = iamanager::v6::IAMCertificateService::NewStub(
        grpc::CreateCustomChannel(mIAMProtectedServerURL, mCredentials, grpc::ChannelArguments()));

    return ErrorEnum::eNone;
}

Error CertificateService::CreateKey(
    const String& nodeID, const String& certType, const String& subject, const String& password, String& csr)
{
    std::lock_guard lock {mMutex};

    LOG_INF() << "Create key" << Log::Field("nodeID", nodeID) << Log::Field("certType", certType)
              << Log::Field("subject", subject);

    try {
        auto ctx = std::make_unique<grpc::ClientContext>();
        ctx->set_deadline(std::chrono::system_clock::now() + cServiceTimeout);

        iamanager::v6::CreateKeyRequest  request;
        iamanager::v6::CreateKeyResponse response;

        request.set_node_id(nodeID.CStr());
        request.set_type(certType.CStr());
        request.set_subject(subject.CStr());
        request.set_password(password.CStr());

        if (auto status = mStub->CreateKey(ctx.get(), request, &response); !status.ok()) {
            return Error(ErrorEnum::eRuntime, status.error_message().c_str());
        }

        if (response.has_error()) {
            return Error(response.error().exit_code(), response.error().message().c_str());
        }

        return AOS_ERROR_WRAP(csr.Assign(response.csr().c_str()));
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(utils::ToAosError(e, ErrorEnum::eRuntime));
    }
}

Error CertificateService::ApplyCert(
    const String& nodeID, const String& certType, const String& pemCert, CertInfo& certInfo)
{
    std::lock_guard lock {mMutex};

    LOG_INF() << "Apply certificate" << Log::Field("nodeID", nodeID) << Log::Field("certType", certType);

    try {
        auto ctx = std::make_unique<grpc::ClientContext>();
        ctx->set_deadline(std::chrono::system_clock::now() + cServiceTimeout);

        iamanager::v6::ApplyCertRequest  request;
        iamanager::v6::ApplyCertResponse response;

        request.set_node_id(nodeID.CStr());
        request.set_type(certType.CStr());
        request.set_cert(pemCert.CStr());

        if (auto status = mStub->ApplyCert(ctx.get(), request, &response); !status.ok()) {
            return Error(ErrorEnum::eRuntime, status.error_message().c_str());
        }

        if (response.has_error()) {
            return Error(response.error().exit_code(), response.error().message().c_str());
        }

        return AOS_ERROR_WRAP(pbconvert::ConvertToAos(response.cert_info(), certInfo));
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(utils::ToAosError(e, ErrorEnum::eRuntime));
    }
}

} // namespace aos::common::iamclient
