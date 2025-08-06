/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_UTILS_PKCS11HELPER_HPP_
#define AOS_COMMON_UTILS_PKCS11HELPER_HPP_

#include <string>

#include <openssl/types.h>

#include <core/common/tools/string.hpp>
#include <core/iam/certhandler/certprovider.hpp>

namespace aos::common::utils {

/**
 * Creates PKCS11 URL.
 *
 * @param keyURL key URL.
 * @param type pkcs11 type = {public, private, cert}.
 * @return RetWithError<std::string> PKCS11 URL.
 */
RetWithError<std::string> CreatePKCS11URL(const String& keyURL, const String& type = "private");

/**
 * Encodes PKCS11 URL in PEM.
 *
 * @param url PKCS11 URL.
 * @return RetWithError<std::string>.
 */
RetWithError<std::string> PEMEncodePKCS11URL(const std::string& url);

/**
 * Configures SSL context with the provided cert type.
 *
 * @param certType cert type.
 * @param caCertPath CA certificate path.
 * @param certProvider certificate provider.
 * @param certLoader certificate loader.
 * @param cryptoProvider crypto provider.
 * @param[out] ctx SSL context.
 * @return Error
 */
Error ConfigureSSLContext(const String& certType, const String& caCertPath,
    iam::certhandler::CertProviderItf& certProvider, crypto::CertLoaderItf& certLoader,
    crypto::x509::ProviderItf& cryptoProvider, SSL_CTX* ctx);

} // namespace aos::common::utils

#endif
