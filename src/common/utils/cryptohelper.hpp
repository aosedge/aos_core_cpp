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
 * Certificate type of the trusted root certificate storage (PKCS11 root cert module).
 * Matches aos::iam::provisionmanager::ProvisionManager's root cert type convention.
 */
constexpr auto cRootCertType = "rootcerts";

/**
 * Loads certificates from the URL and converts them to PEM format.
 *
 * @param certURL URL to the certificate.
 * @param certLoader certificate loader.
 * @param cryptoProvider crypto provider.
 * @return RetWithError<std::string> PEM certificates.
 */
RetWithError<std::string> LoadPEMCertificates(
    const String& certURL, crypto::CertLoaderItf& certLoader, crypto::x509::ProviderItf& cryptoProvider);

/**
 * Loads trusted root certificates from the root cert storage and converts them to PEM format.
 *
 * @param certProvider certificate provider.
 * @param certLoader certificate loader.
 * @param cryptoProvider crypto provider.
 * @param rootCertType certificate type of the trusted root certificate storage.
 * @return RetWithError<std::string> concatenated root certificates in PEM format.
 */
RetWithError<std::string> LoadRootCertificates(const iamclient::CertProviderItf& certProvider,
    crypto::CertLoaderItf& certLoader, crypto::x509::ProviderItf& cryptoProvider,
    const String& rootCertType = cRootCertType);

/**
 * Returns a human-readable OpenSSL error string.
 *
 * @return std::string.
 */
std::string GetOpensslErrorString();

/**
 * Loads root CA certificate(s) from PEM into SSL context.
 *
 * @param rootCertsPem root certificates in PEM format.
 * @param ctx SSL context.
 * @return Error.
 */
Error LoadRootCertsToSSLContext(const std::string& rootCertsPem, SSL_CTX* ctx);

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
