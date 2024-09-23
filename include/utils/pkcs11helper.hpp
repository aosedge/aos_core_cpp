/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PKCS11HELPER_HPP_
#define PKCS11HELPER_HPP_

#include <string>

#include <aos/common/tools/string.hpp>

namespace aos::common::utils {

/**
 * Creates PKCS11 URL from the key URL.
 *
 * @param keyURL key URL.
 * @return RetWithError<std::string> PKCS11 URL.
 */
RetWithError<std::string> CreatePKCS11URL(const String& keyURL);

} // namespace aos::common::utils

#endif /* PKCS11HELPER_HPP_ */
