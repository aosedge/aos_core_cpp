/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <regex>

#include <aos/common/crypto/utils.hpp>

#include "pk11uri.hpp"
#include "utils/exception.hpp"
#include "utils/pkcs11helper.hpp"

namespace aos::common::utils {

/***********************************************************************************************************************
 * Statics
 **********************************************************************************************************************/

// Creates RFC7512 URL adapted for PKCS11 provider:
// https://www.rfc-editor.org/rfc/rfc7512.html
static std::string CreatePKCS11ProviderURL(const String& url, const String& type)
{
    std::string result;

    try {
        // libp11 v0.4.11(provided with Ubuntu 22.04) loads pkcs11 objects with invalid id if label available.
        // Remove label to protect against loading invalid objects.
        std::regex objLabelRegex {"object=[^&?;]*[&?;]?"};

        result = std::regex_replace(url.CStr(), objLabelRegex, "");

        // pkcs11-provider doesn't process module-path
        std::regex modulePathRegex {"module\\-path=[^&?;]*[&?;]?"};

        result = std::regex_replace(result, modulePathRegex, "");

        // pkcs11-provider tools/uri2pem.py requires type=private, make url compatible with it.
        std::regex  pkcs11PrefixRegex {"^pkcs11:"};
        std::string pkcs11Prefix = std::string("pkcs11:type=") + type.CStr() + ";";

        result = std::regex_replace(result, pkcs11PrefixRegex, pkcs11Prefix);
    } catch (const std::exception& e) {
        AOS_ERROR_THROW(ErrorEnum::eFailed, e.what());
    }

    return result;
}

/***********************************************************************************************************************
 * Public functions
 **********************************************************************************************************************/

RetWithError<std::string> CreatePKCS11URL(const String& keyURL, const String& type)
{
    try {
        return {CreatePKCS11ProviderURL(keyURL, type), ErrorEnum::eNone};
    } catch (const std::exception& e) {
        return {"", AOS_ERROR_WRAP(utils::ToAosError(e))};
    }
}

RetWithError<std::string> PEMEncodePKCS11URL(const std::string& url)
{
    using P11UriPtr = std::unique_ptr<P11PROV_PK11_URI, decltype(&P11PROV_PK11_URI_free)>;
    using BioPtr    = std::unique_ptr<BIO, decltype(&BIO_free)>;

    auto pk11uri = P11UriPtr(P11PROV_PK11_URI_new(), &P11PROV_PK11_URI_free);

    if (!ASN1_STRING_set(pk11uri->desc, cP11ProvDescURIFile, strlen(cP11ProvDescURIFile))) {
        return {"", AOS_ERROR_WRAP(ErrorEnum::eFailed)};
    }

    if (!ASN1_STRING_set(pk11uri->uri, url.c_str(), url.length())) {
        return {"", AOS_ERROR_WRAP(ErrorEnum::eFailed)};
    }

    auto bio = BioPtr(BIO_new(BIO_s_mem()), &BIO_free);
    if (bio == nullptr) {
        return {"", AOS_ERROR_WRAP(ErrorEnum::eFailed)};
    }

    if (PEM_write_bio_P11PROV_PK11_URI(bio.get(), pk11uri.get()) != 1) {
        return {"", AOS_ERROR_WRAP(ErrorEnum::eFailed)};
    }

    char* data = nullptr;
    long  len  = BIO_get_mem_data(bio.get(), &data);

    return std::string {data, static_cast<size_t>(len)};
}

} // namespace aos::common::utils
