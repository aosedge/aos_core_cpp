/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <regex>

#include <aos/common/crypto/utils.hpp>

#include "utils/exception.hpp"
#include "utils/pkcs11helper.hpp"

namespace aos::common::utils {

/***********************************************************************************************************************
 * Statics
 **********************************************************************************************************************/

// LibP11 has its limitations on RFC7512 urls:
// https://www.rfc-editor.org/rfc/rfc7512.html
static std::string CreateLibP11PKCS11URL(const String& url)
{
    std::string result;

    try {
        // libp11 v0.4.11(provided with Ubuntu 22.04) loads pkcs11 objects with invalid id if label available.
        // Remove label to protect against loading invalid objects.
        std::regex objLabelRegex {"object=[^&?;]*[&?;]?"};

        result = std::regex_replace(url.CStr(), objLabelRegex, "");

        // libp11 doesn't process module-path
        std::regex modulePathRegex {"module\\-path=[^&?;]*[&?;]?"};

        result = std::regex_replace(result, modulePathRegex, "");
    } catch (const std::exception& exc) {
        AOS_ERROR_THROW(exc.what(), aos::ErrorEnum::eFailed);
    }

    return result;
}

/***********************************************************************************************************************
 * Public functions
 **********************************************************************************************************************/

RetWithError<std::string> CreatePKCS11URL(const String& keyURL)
{
    try {
        return {CreateLibP11PKCS11URL(keyURL), ErrorEnum::eNone};
    } catch (const std::exception& e) {
        return {"", utils::ToAosError(e)};
    }
}

} // namespace aos::common::utils
