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
#include <core/common/types/certificates.hpp>

namespace aos::common::utils {

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
 * @return RetWithError<std::string> concatenated root certificates in PEM format.
 */
RetWithError<std::string> LoadRootCertificates(const ::aos::iamclient::CertProviderItf& certProvider,
    crypto::CertLoaderItf& certLoader, crypto::x509::ProviderItf& cryptoProvider);

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
 * Collects SHA-256 fingerprints of the trusted root certificates.
 *
 * @param certProvider certificate provider.
 * @param certLoader certificate loader.
 * @param[out] fingerprints root certificate SHA-256 fingerprints.
 * @return Error.
 */
Error GetRootCertsFingerprints(const ::aos::iamclient::CertProviderItf& certProvider, crypto::CertLoaderItf& certLoader,
    Array<SHA256Fingerprint>& fingerprints);

/**
 * Configures SSL context with the provided cert type.
 *
 * @param storageID cert storage ID.
 * @param certProvider certificate provider.
 * @param certLoader certificate loader.
 * @param cryptoProvider crypto provider.
 * @param[out] ctx SSL context.
 * @return Error
 */
Error ConfigureSSLContext(const String& storageID, const ::aos::iamclient::CertProviderItf& certProvider,
    crypto::CertLoaderItf& certLoader, crypto::x509::ProviderItf& cryptoProvider, SSL_CTX* ctx);

/**
 * Subscribes a listener to service certificate and trusted root certificate changes.
 *
 * @param certProvider certificate provider.
 * @param storageID service certificate type. Empty subscribes to root certificates only.
 * @param listener certificate listener.
 * @return Error.
 */
Error SubscribeCertChanges(::aos::iamclient::CertProviderItf& certProvider, const String& storageID,
    ::aos::iamclient::CertListenerItf& listener);

} // namespace aos::common::utils

#endif
