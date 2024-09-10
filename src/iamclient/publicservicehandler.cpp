/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <utils/grpchelper.hpp>

#include "logger/logmodule.hpp"
#include "publicservicehandler.hpp"

namespace aos::mp::iamclient {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error PublicServiceHandler::Init(const config::Config& cfg, cryptoutils::CertLoaderItf& certLoader,
    crypto::x509::ProviderItf& cryptoProvider, bool insecureConnection, MTLSCredentialsFunc mtlsCredentialsFunc)
{
    LOG_INF() << "Initializing public service handler: insecureConnection=" << insecureConnection;

    mConfig              = &cfg;
    mCertLoader          = &certLoader;
    mCryptoProvider      = &cryptoProvider;
    mMTLSCredentialsFunc = std::move(mtlsCredentialsFunc);

    if (auto err = CreateCredentials(insecureConnection); !err.IsNone()) {
        return err;
    }

    return ErrorEnum::eNone;
}

RetWithError<std::shared_ptr<grpc::ChannelCredentials>> PublicServiceHandler::GetMTLSConfig(
    const std::string& certStorage)
{
    iam::certhandler::CertInfo certInfo;

    LOG_DBG() << "Getting MTLS config: certStorage=" << certStorage.c_str();

    if (auto err = GetCertificate(certStorage, certInfo); !err.IsNone()) {
        return {nullptr, err};
    }

    return {mMTLSCredentialsFunc(certInfo, mConfig->mCACert.c_str(), *mCertLoader, *mCryptoProvider), ErrorEnum::eNone};
}

std::shared_ptr<grpc::ChannelCredentials> PublicServiceHandler::GetTLSCredentials()
{
    if (!mConfig->mCACert.empty()) {
        LOG_DBG() << "Getting TLS config";

        return common::utils::GetTLSClientCredentials(mConfig->mCACert.c_str());
    }

    return nullptr;
}

Error PublicServiceHandler::GetCertificate(const std::string& certType, iam::certhandler::CertInfo& certInfo)
{
    IAMPublicServicePtr stub = iamanager::v5::IAMPublicService::NewStub(
        grpc::CreateCustomChannel(mConfig->mIAMConfig.mIAMPublicServerURL, mCredentials, grpc::ChannelArguments()));
    auto ctx = std::make_unique<grpc::ClientContext>();

    ctx->set_deadline(std::chrono::system_clock::now() + cIAMPublicServiceTimeout);

    iamanager::v5::GetCertRequest  request;
    iamanager::v5::GetCertResponse response;

    request.set_type(certType);

    if (auto status = stub->GetCert(ctx.get(), request, &response); !status.ok()) {
        LOG_ERR() << "Failed to get certificate: error=" << status.error_message().c_str();

        return ErrorEnum::eRuntime;
    }

    certInfo.mCertURL = response.cert_url().c_str();
    certInfo.mKeyURL  = response.key_url().c_str();

    LOG_DBG() << "Certificate received: certURL=" << certInfo.mCertURL.CStr() << ", keyURL=" << certInfo.mKeyURL.CStr();

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

Error PublicServiceHandler::CreateCredentials(bool insecureConnection)
{
    try {
        if (insecureConnection) {
            mCredentials = grpc::InsecureChannelCredentials();

            return ErrorEnum::eNone;
        }

        mCredentials = common::utils::GetTLSClientCredentials(mConfig->mCACert.c_str());
    } catch (const std::exception& e) {
        LOG_ERR() << "Failed to create credentials: error=" << e.what();

        return ErrorEnum::eRuntime;
    }

    return ErrorEnum::eNone;
}

} // namespace aos::mp::iamclient
