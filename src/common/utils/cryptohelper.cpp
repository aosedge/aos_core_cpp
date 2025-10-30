/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <memory>
#include <numeric>
#include <sstream>

#include <openssl/err.h>
#include <openssl/sha.h>
#include <openssl/ssl.h>

#include "cryptohelper.hpp"
#include "exception.hpp"
#include "pkcs11helper.hpp"

namespace aos::common::utils {

/***********************************************************************************************************************
 * Statics
 **********************************************************************************************************************/

namespace {

std::string ConvertCertificateToPEM(
    const crypto::x509::Certificate& certificate, crypto::x509::ProviderItf& cryptoProvider)
{
    std::string result(crypto::cCertPEMLen, '0');
    String      view = result.c_str();

    auto err = cryptoProvider.X509CertToPEM(certificate, view);
    AOS_ERROR_CHECK_AND_THROW(err, "Certificate conversion problem");

    result.resize(view.Size());

    return result;
}

std::string ConvertCertificatesToPEM(
    const Array<crypto::x509::Certificate>& chain, crypto::x509::ProviderItf& cryptoProvider)
{
    return std::accumulate(
        chain.begin(), chain.end(), std::string {}, [&](const std::string& result, const auto& cert) {
            return result + ConvertCertificateToPEM(cert, cryptoProvider);
        });
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

RetWithError<std::string> LoadPEMCertificates(
    const String& certURL, crypto::CertLoaderItf& certLoader, crypto::x509::ProviderItf& cryptoProvider)
{
    try {
        auto [certificates, err] = certLoader.LoadCertsChainByURL(certURL);
        if (!err.IsNone()) {
            return {"", Error(err, "Load certificate by URL failed")};
        }

        auto chain = Array<crypto::x509::Certificate>(certificates->begin(), certificates->Size());

        return {ConvertCertificatesToPEM(chain, cryptoProvider), ErrorEnum::eNone};
    } catch (const std::exception& e) {
        return {"", AOS_ERROR_WRAP(utils::ToAosError(e))};
    }
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

Error ConfigureSSLContext(const String& certType, const String& caCertPath,
    const iamclient::CertProviderItf& certProvider, crypto::CertLoaderItf& certLoader,
    crypto::x509::ProviderItf& cryptoProvider, SSL_CTX* ctx)
{
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);

    auto certInfo = std::make_unique<CertInfo>();

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
