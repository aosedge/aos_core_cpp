/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <common/logger/logmodule.hpp>

#include "tlscredentials.hpp"

namespace aos::common::iamclient {
/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error TLSCredentials::Init(const std::string& caCert, PublicServiceItf& publicservice,
    crypto::CertLoaderItf& certLoader, crypto::x509::ProviderItf& cryptoProvider,
    MTLSCredentialsFunc mtlsCredentialsFunc)
{
    mPublicService       = &publicservice;
    mMTLSCredentialsFunc = std::move(mtlsCredentialsFunc);
    mCACert              = caCert;
    mCertLoader          = &certLoader;
    mCryptoProvider      = &cryptoProvider;

    return ErrorEnum::eNone;
}

RetWithError<std::shared_ptr<grpc::ChannelCredentials>> TLSCredentials::GetMTLSClientCredentials(
    const String& certStorage)
{
    iam::certhandler::CertInfo certInfo;

    LOG_DBG() << "Get MTLS config: certStorage=" << certStorage;

    if (auto err = mPublicService->GetCert(certStorage, {}, {}, certInfo); !err.IsNone()) {
        return {nullptr, err};
    }

    return {mMTLSCredentialsFunc(certInfo, mCACert.c_str(), *mCertLoader, *mCryptoProvider), ErrorEnum::eNone};
}

RetWithError<std::shared_ptr<grpc::ChannelCredentials>> TLSCredentials::GetTLSClientCredentials()
{
    LOG_DBG() << "Get TLS config";

    if (!mCACert.empty()) {
        return {common::utils::GetTLSClientCredentials(mCACert.c_str()), ErrorEnum::eNone};
    }

    return {nullptr, ErrorEnum::eNone};
}

} // namespace aos::common::iamclient
