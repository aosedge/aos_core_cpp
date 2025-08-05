/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_UTILS_CRYPTOHELPER_HPP_
#define AOS_COMMON_UTILS_CRYPTOHELPER_HPP_

#include <string>

#include <core/common/crypto/crypto.hpp>
#include <core/common/crypto/cryptoutils.hpp>
#include <core/common/tools/array.hpp>
#include <core/common/tools/error.hpp>
#include <core/common/tools/string.hpp>

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

} // namespace aos::common::utils

#endif
