/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_UTILS_PKCS11HELPER_HPP_
#define AOS_COMMON_UTILS_PKCS11HELPER_HPP_

#include <string>

#include <aos/common/tools/string.hpp>

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

} // namespace aos::common::utils

#endif
