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

Error TLSCredentials::Init(const std::string& caCert, aos::iamclient::CertProviderItf& certProvider,
    crypto::CertLoaderItf& certLoader, crypto::x509::ProviderItf& cryptoProvider)
{
    LOG_DBG() << "Init TLS credentials";

    mCertProvider   = &certProvider;
    mCACert         = caCert;
    mCertLoader     = &certLoader;
    mCryptoProvider = &cryptoProvider;

    return ErrorEnum::eNone;
}

RetWithError<std::shared_ptr<grpc::ChannelCredentials>> TLSCredentials::GetMTLSClientCredentials(
    const String& certStorage, bool insecureConnection)
{
    LOG_DBG() << "Get MTLS config" << Log::Field("certStorage", certStorage);

    if (insecureConnection) {
        return {grpc::InsecureChannelCredentials(), ErrorEnum::eNone};
    }

    CertInfo certInfo;

    if (auto err = mCertProvider->GetCert(certStorage, {}, {}, certInfo); !err.IsNone()) {
        return {nullptr, err};
    }

    return {common::utils::GetMTLSClientCredentials(certInfo, mCACert.c_str(), *mCertLoader, *mCryptoProvider),
        ErrorEnum::eNone};
}

RetWithError<std::shared_ptr<grpc::ChannelCredentials>> TLSCredentials::GetTLSClientCredentials(bool insecureConnection)
{
    LOG_DBG() << "Get TLS config";

    if (insecureConnection) {
        return {grpc::InsecureChannelCredentials(), ErrorEnum::eNone};
    }

    if (!mCACert.empty()) {
        return {common::utils::GetTLSClientCredentials(mCACert.c_str()), ErrorEnum::eNone};
    }

    return {nullptr, ErrorEnum::eNotFound};
}

} // namespace aos::common::iamclient
