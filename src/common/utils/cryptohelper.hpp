/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_UTILS_CRYPTOHELPER_HPP_
#define AOS_COMMON_UTILS_CRYPTOHELPER_HPP_

#include <string>

#include <openssl/types.h>

#include <core/common/crypto/itf/certloader.hpp>
#include <core/common/crypto/itf/crypto.hpp>
#include <core/common/iamclient/itf/certprovider.hpp>

namespace aos::common::utils {

/**
 * PEM certificates loaded from a certificate chain.
 */
struct PEMCertificates {
    std::string mRootCert;
    std::string mCertChain;
};

/**
 * Loads root CA certificate from PEM into SSL context.
 *
 * @param rootCertPem root certificate in PEM format.
 * @param ctx SSL context.
 * @return Error.
 */
Error LoadRootCertToSSLContext(const std::string& rootCertPem, SSL_CTX* ctx);

/**
 * Loads certificates from the URL and converts them to PEM format.
 *
 * If the chain contains multiple certificates, the last one is treated as the root certificate
 * and returned in @ref PEMCertificates::mRootCert. The remaining certificates are returned in
 * @ref PEMCertificates::mCertChain.
 *
 * @param certURL URL to the certificate.
 * @param certLoader certificate loader.
 * @param cryptoProvider crypto provider.
 * @return RetWithError<PEMCertificates> root and identity certificate chain in PEM format.
 */
RetWithError<PEMCertificates> LoadPEMCertificates(
    const String& certURL, crypto::CertLoaderItf& certLoader, crypto::x509::ProviderItf& cryptoProvider);

/**
 * Returns a human-readable OpenSSL error string.
 *
 * @return std::string.
 */
std::string GetOpensslErrorString();

/**
 * Configures SSL context with the provided cert type.
 *
 * @param certType cert type.
 * @param certProvider certificate provider.
 * @param certLoader certificate loader.
 * @param cryptoProvider crypto provider.
 * @param[out] ctx SSL context.
 * @return Error
 */
Error ConfigureSSLContext(const String& certType, const iamclient::CertProviderItf& certProvider,
    crypto::CertLoaderItf& certLoader, crypto::x509::ProviderItf& cryptoProvider, SSL_CTX* ctx);

} // namespace aos::common::utils

#endif
