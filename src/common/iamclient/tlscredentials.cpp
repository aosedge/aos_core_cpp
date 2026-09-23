/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <core/common/tools/logger.hpp>

#include <common/utils/cryptohelper.hpp>

#include "tlscredentials.hpp"

namespace aos::common::iamclient {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error TLSCredentials::Init(aos::iamclient::CertProviderItf& certProvider, crypto::CertLoaderItf& certLoader,
    crypto::x509::ProviderItf& cryptoProvider)
{
    LOG_DBG() << "Init TLS credentials";

    mCertProvider   = &certProvider;
    mCertLoader     = &certLoader;
    mCryptoProvider = &cryptoProvider;

    return ErrorEnum::eNone;
}

RetWithError<std::shared_ptr<grpc::ChannelCredentials>> TLSCredentials::GetMTLSClientCredentials(
    const String& certStorage)
{
    LOG_DBG() << "Get MTLS config" << Log::Field("certStorage", certStorage);

    auto certInfo = std::make_unique<CertInfo>();

    if (auto err = mCertProvider->GetCert(certStorage, {}, {}, *certInfo); !err.IsNone()) {
        return {nullptr, err};
    }

    auto [rootCertsPem, rootErr] = common::utils::LoadRootCertificates(*mCertProvider, *mCertLoader, *mCryptoProvider);
    if (!rootErr.IsNone()) {
        return {nullptr, rootErr};
    }

    return {common::utils::GetMTLSClientCredentials(*certInfo, rootCertsPem, *mCertLoader, *mCryptoProvider),
        ErrorEnum::eNone};
}

RetWithError<std::shared_ptr<grpc::ChannelCredentials>> TLSCredentials::GetTLSClientCredentials()
{
    LOG_DBG() << "Get TLS config";

    auto [rootCertsPem, err] = common::utils::LoadRootCertificates(*mCertProvider, *mCertLoader, *mCryptoProvider);
    if (!err.IsNone()) {
        return {nullptr, err};
    }

    return {common::utils::GetTLSClientCredentials(rootCertsPem), ErrorEnum::eNone};
}

} // namespace aos::common::iamclient
