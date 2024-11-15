/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CRYPTOHELPER_HPP_
#define CRYPTOHELPER_HPP_

#include <string>

#include <aos/common/crypto/crypto.hpp>
#include <aos/common/crypto/utils.hpp>
#include <aos/common/tools/array.hpp>
#include <aos/common/tools/error.hpp>
#include <aos/common/tools/string.hpp>

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

#endif /* CRYPTOHELPER_HPP_ */
