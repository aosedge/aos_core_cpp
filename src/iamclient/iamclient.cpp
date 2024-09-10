/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "iamclient.hpp"
#include "logger/logmodule.hpp"

namespace aos::mp::iamclient {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error IAMClient::Init(const config::Config& cfg, cryptoutils::CertLoaderItf& certLoader,
    crypto::x509::ProviderItf& cryptoProvider, bool provisioningMode, MTLSCredentialsFunc mtlsCredentialsFunc)
{
    LOG_INF() << "Initializing IAM client";

    mPublicServiceHandler.emplace();
    mPublicNodeClient.emplace();
    mProtectedNodeClient.emplace();

    if (auto err = mPublicServiceHandler->Init(
            cfg, certLoader, cryptoProvider, provisioningMode, std::move(mtlsCredentialsFunc));
        !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = mPublicNodeClient->Init(cfg.mIAMConfig, *this, true); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (!provisioningMode) {
        if (auto err = mProtectedNodeClient->Init(cfg.mIAMConfig, *this, false); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    return ErrorEnum::eNone;
}

RetWithError<std::shared_ptr<grpc::ChannelCredentials>> IAMClient::GetMTLSConfig(const std::string& certStorage)
{
    LOG_DBG() << "Getting MTLS config: certStorage=" << certStorage.c_str();

    return mPublicServiceHandler->GetMTLSConfig(certStorage);
}

std::shared_ptr<grpc::ChannelCredentials> IAMClient::GetTLSCredentials()
{
    LOG_DBG() << "Getting TLS config";

    return mPublicServiceHandler->GetTLSCredentials();
}

Error IAMClient::GetCertificate(const std::string& certType, iam::certhandler::CertInfo& certInfo)
{
    LOG_DBG() << "Getting certificate: certType=" << certType.c_str();

    return mPublicServiceHandler->GetCertificate(certType, certInfo);
}

} // namespace aos::mp::iamclient
