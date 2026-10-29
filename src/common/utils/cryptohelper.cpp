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

RetWithError<std::string> LoadRootCertificates(const ::aos::iamclient::CertProviderItf& certProvider,
    crypto::CertLoaderItf& certLoader, crypto::x509::ProviderItf& cryptoProvider)
{
    try {
        auto certInfos = std::make_unique<StaticArray<CertInfo, crypto::cMaxRootCerts>>();

        if (auto err = certProvider.GetRootCerts(*certInfos); !err.IsNone()) {
            return {"", AOS_ERROR_WRAP(Error(err, "load root certificates failed"))};
        }

        std::string rootCertsPem;

        for (const auto& certInfo : *certInfos) {
            auto [rootCert, err] = LoadPEMCertificates(certInfo.mCertURL, certLoader, cryptoProvider);
            if (!err.IsNone()) {
                return {"", AOS_ERROR_WRAP(err)};
            }

            rootCertsPem += rootCert;
        }

        if (rootCertsPem.empty()) {
            return {"", AOS_ERROR_WRAP(ErrorEnum::eNotFound)};
        }

        return {rootCertsPem, ErrorEnum::eNone};
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

Error GetRootCertsFingerprints(const ::aos::iamclient::CertProviderItf& certProvider, crypto::CertLoaderItf& certLoader,
    Array<SHA256Fingerprint>& fingerprints)
{
    auto certInfos = std::make_unique<StaticArray<CertInfo, crypto::cMaxRootCerts>>();

    if (auto err = certProvider.GetRootCerts(*certInfos); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    fingerprints.Clear();

    for (const auto& certInfo : *certInfos) {
        auto [certificates, err] = certLoader.LoadCertsChainByURL(certInfo.mCertURL);
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (certificates->IsEmpty()) {
            return AOS_ERROR_WRAP(ErrorEnum::eNotFound);
        }

        const auto&                               raw = (*certificates)[0].mRaw;
        StaticArray<uint8_t, crypto::cSHA256Size> hash;

        if (auto resizeErr = hash.Resize(crypto::cSHA256Size); !resizeErr.IsNone()) {
            return AOS_ERROR_WRAP(resizeErr);
        }

        SHA256(raw.Get(), raw.Size(), hash.Get());

        if (auto pushErr = fingerprints.EmplaceBack(); !pushErr.IsNone()) {
            return AOS_ERROR_WRAP(pushErr);
        }

        if (auto hexErr = fingerprints.Back().ByteArrayToHex(hash); !hexErr.IsNone()) {
            return AOS_ERROR_WRAP(hexErr);
        }
    }

    return ErrorEnum::eNone;
}

Error LoadRootCertsToSSLContext(const std::string& rootCertsPem, SSL_CTX* ctx)
{
    BIO* bio = BIO_new_mem_buf(rootCertsPem.c_str(), -1);
    if (!bio) {
        return Error(ErrorEnum::eRuntime, "failed to create BIO");
    }

    std::unique_ptr<BIO, decltype(&BIO_free)> bioPtr(bio, BIO_free);

    X509_STORE* store = SSL_CTX_get_cert_store(ctx);
    if (!store) {
        return Error(ErrorEnum::eRuntime, "failed to get cert store");
    }

    size_t caCount = 0;

    X509* ca = nullptr;
    while ((ca = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr)) != nullptr) {
        std::unique_ptr<X509, decltype(&X509_free)> caPtr(ca, X509_free);

        if (X509_STORE_add_cert(store, ca) != 1) {
            return Error(ErrorEnum::eRuntime, GetOpensslErrorString().c_str());
        }

        ++caCount;
    }

    if (caCount == 0) {
        return Error(ErrorEnum::eRuntime, GetOpensslErrorString().c_str());
    }

    return ErrorEnum::eNone;
}

Error ConfigureSSLContext(const String& storageID, const ::aos::iamclient::CertProviderItf& certProvider,
    crypto::CertLoaderItf& certLoader, crypto::x509::ProviderItf& cryptoProvider, SSL_CTX* ctx)
{
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);

    // Drop file and system CAs. Trusted roots come from the PKCS#11 root certificate storage.
    auto* store = X509_STORE_new();
    if (store == nullptr) {
        return Error(ErrorEnum::eRuntime, "failed to create cert store");
    }

    SSL_CTX_set_cert_store(ctx, store);

    auto certInfo = std::make_unique<CertInfo>();

    if (auto err = certProvider.GetCert(storageID, {}, {}, *certInfo); !err.IsNone()) {
        return err;
    }

    auto [certificate, errLoad] = common::utils::LoadPEMCertificates(certInfo->mCertURL, certLoader, cryptoProvider);
    if (!errLoad.IsNone()) {
        return errLoad;
    }

    auto [rootCertsPem, errRoot] = LoadRootCertificates(certProvider, certLoader, cryptoProvider);
    if (!errRoot.IsNone()) {
        return errRoot;
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

    return LoadRootCertsToSSLContext(rootCertsPem, ctx);
}

Error SubscribeCertChanges(::aos::iamclient::CertProviderItf& certProvider, const String& storageID,
    ::aos::iamclient::CertListenerItf& listener)
{
    if (!storageID.IsEmpty()) {
        if (auto err = certProvider.SubscribeListener(storageID, listener); !err.IsNone()) {
            return err;
        }
    }

    return certProvider.SubscribeRootCertsListener(listener);
}

} // namespace aos::common::utils
