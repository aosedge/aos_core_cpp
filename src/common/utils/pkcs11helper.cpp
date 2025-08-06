/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <regex>

#include <openssl/err.h>
#include <openssl/sha.h>
#include <openssl/ssl.h>

#include <core/common/crypto/cryptoutils.hpp>

#include "cryptohelper.hpp"
#include "exception.hpp"
#include "pk11uri.hpp"
#include "pkcs11helper.hpp"

namespace aos::common::utils {

namespace {

/***********************************************************************************************************************
 * Statics
 **********************************************************************************************************************/

// Creates RFC7512 URL adapted for PKCS11 provider:
// https://www.rfc-editor.org/rfc/rfc7512.html
std::string CreatePKCS11ProviderURL(const String& url, const String& type)
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

std::string GetOpensslErrorString()
{
    std::ostringstream oss;
    unsigned long      errCode;

    while ((errCode = ERR_get_error()) != 0) {
        char buf[256];

        ERR_error_string_n(errCode, buf, sizeof(buf));
        oss << buf << std::endl;
    }

    return oss.str();
}

RetWithError<EVP_PKEY*> LoadPrivateKey(const std::string& keyURL)
{
    auto [pkcs11URL, createErr] = common::utils::CreatePKCS11URL(keyURL.c_str());
    if (!createErr.IsNone()) {
        return {nullptr, createErr};
    }

    auto [pem, encodeErr] = common::utils::PEMEncodePKCS11URL(pkcs11URL);
    if (!encodeErr.IsNone()) {
        return {nullptr, encodeErr};
    }

    auto bio = DeferRelease(BIO_new_mem_buf(pem.c_str(), pem.length()), BIO_free);
    if (!bio) {
        return {nullptr, AOS_ERROR_WRAP(Error(ErrorEnum::eRuntime, GetOpensslErrorString().c_str()))};
    }

    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio.Get(), NULL, NULL, NULL);
    if (!pkey) {
        return {nullptr, AOS_ERROR_WRAP(Error(ErrorEnum::eRuntime, GetOpensslErrorString().c_str()))};
    }

    return {pkey, ErrorEnum::eNone};
}

} // namespace

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

Error ConfigureSSLContext(const String& certType, const String& caCertPath,
    iam::certhandler::CertProviderItf& certProvider, crypto::CertLoaderItf& certLoader,
    crypto::x509::ProviderItf& cryptoProvider, SSL_CTX* ctx)
{
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);

    auto certInfo = std::make_unique<iam::certhandler::CertInfo>();

    if (auto err = certProvider.GetCert(certType, {}, {}, *certInfo); !err.IsNone()) {
        return err;
    }

    auto [certificate, errLoad] = common::utils::LoadPEMCertificates(certInfo->mCertURL, certLoader, cryptoProvider);
    if (!errLoad.IsNone()) {
        return errLoad;
    }

    auto [pkey, errLoadKey] = LoadPrivateKey(certInfo->mKeyURL.CStr());
    if (!errLoadKey.IsNone()) {
        return errLoadKey;
    }

    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> pkeyPtr(pkey, EVP_PKEY_free);
    if (SSL_CTX_use_PrivateKey(ctx, pkey) <= 0) {
        return Error(ErrorEnum::eRuntime, GetOpensslErrorString().c_str());
    }

    BIO* bio = BIO_new_mem_buf(certificate.c_str(), -1);
    if (!bio) {
        return Error(ErrorEnum::eRuntime, "failed to create BIO");
    }

    std::unique_ptr<BIO, decltype(&BIO_free)> bioPtr(bio, BIO_free);

    X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    if (!cert) {
        return Error(ErrorEnum::eRuntime, GetOpensslErrorString().c_str());
    }

    std::unique_ptr<X509, decltype(&X509_free)> certPtr(cert, X509_free);

    if (SSL_CTX_use_certificate(ctx, cert) <= 0) {
        return Error(ErrorEnum::eRuntime, GetOpensslErrorString().c_str());
    }

    auto chain_deleter = [](STACK_OF(X509) * chain) { sk_X509_pop_free(chain, X509_free); };
    std::unique_ptr<STACK_OF(X509), decltype(chain_deleter)> chain(sk_X509_new_null(), chain_deleter);

    X509* intermediateCert = nullptr;
    while ((intermediateCert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr)) != nullptr) {
        sk_X509_push(chain.get(), intermediateCert);
    }

    if (sk_X509_num(chain.get()) > 0 && SSL_CTX_set1_chain(ctx, chain.get()) <= 0) {
        return Error(ErrorEnum::eRuntime, GetOpensslErrorString().c_str());
    }

    if (SSL_CTX_load_verify_locations(ctx, caCertPath.CStr(), nullptr) <= 0) {
        return Error(ErrorEnum::eRuntime, GetOpensslErrorString().c_str());
    }

    return ErrorEnum::eNone;
}

} // namespace aos::common::utils
