/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <memory>
#include <sstream>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/trace.h>

#include <common/logger/logmodule.hpp>
#include <common/utils/cryptohelper.hpp>
#include <common/utils/pkcs11helper.hpp>

#include "securechannel.hpp"

namespace aos::mp::communication {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

SecureChannel::SecureChannel(const config::Config& cfg, CommChannelItf& channel,
    common::iamclient::TLSCredentialsItf& certProvider, crypto::CertLoaderItf& certLoader,
    crypto::x509::ProviderItf& cryptoProvider, int port, const std::string& certStorage)
    : mChannel(&channel)
    , mCertProvider(&certProvider)
    , mCertLoader(&certLoader)
    , mCryptoProvider(&cryptoProvider)
    , mCfg(&cfg)
    , mPort(port)
    , mCertStorage(certStorage)
{
    LOG_DBG() << "Create secure channel: port=" << mPort;

    InitOpenssl();
}

SecureChannel::~SecureChannel()
{
    LOG_DBG() << "Destroy secure channel: port=" << mPort;

    CleanupOpenssl();
}

Error SecureChannel::Connect()
{
    LOG_DBG() << "Connect to secure channel: port=" << mPort;

    if (mConnected) {
        return ErrorEnum::eNone;
    }

    if (auto err = mChannel->Connect(); !err.IsNone()) {
        return err;
    }

    if (mSSL != nullptr) {
        SSL_free(mSSL);
        mSSL = nullptr;
    }

    if (mCtx != nullptr) {
        SSL_CTX_free(mCtx);
        mCtx = nullptr;
    }

    mBioMethod.reset(nullptr);
    mBioMethod.reset(BIO_meth_new(BIO_TYPE_SOURCE_SINK, "SecureChannel BIO"));
    if (!mBioMethod) {
        return Error(ErrorEnum::eRuntime, "failed to create BIO method");
    }

    BIO_meth_set_write(mBioMethod.get(), CustomBIOWrite);
    BIO_meth_set_read(mBioMethod.get(), CustomBIORead);
    BIO_meth_set_ctrl(mBioMethod.get(), CustomBIOCtrl);

    const SSL_METHOD* method = TLS_server_method();
    mCtx                     = CreateSSLContext(method);
    if (!mCtx) {
        return Error(ErrorEnum::eRuntime, "failed to create SSL context");
    }

    if (auto err = ConfigureSSLContext(mCtx); !err.IsNone()) {
        SSL_CTX_free(mCtx);
        mCtx = nullptr;

        return err;
    }

    mSSL = SSL_new(mCtx);
    if (!mSSL) {
        return Error(ErrorEnum::eRuntime, GetOpensslErrorString().c_str());
    }

    BIO* rbio = BIO_new(mBioMethod.get());
    BIO* wbio = BIO_new(mBioMethod.get());

    if (!rbio || !wbio) {
        BIO_free(rbio);
        BIO_free(wbio);

        return Error(ErrorEnum::eRuntime, "failed to create BIO objects");
    }

    BIO_set_data(rbio, this);
    BIO_set_data(wbio, this);
    SSL_set_bio(mSSL, rbio, wbio);

    if (SSL_accept(mSSL) <= 0) {
        LOG_ERR() << "Failed to accept SSL connection";
        SSL_free(mSSL);
        mSSL = nullptr;

        return Error(ErrorEnum::eRuntime, GetOpensslErrorString().c_str());
    }

    mConnected = true;
    LOG_DBG() << "SSL connection accepted";

    return ErrorEnum::eNone;
}

Error SecureChannel::Read(std::vector<uint8_t>& message)
{
    if (message.empty()) {
        return Error(ErrorEnum::eRuntime, "message buffer is empty");
    }

    int bytesRead = SSL_read(mSSL, message.data(), message.size());
    if (bytesRead <= 0) {
        return Error(ErrorEnum::eRuntime, GetOpensslErrorString().c_str());
    }

    return ErrorEnum::eNone;
}

Error SecureChannel::Write(std::vector<uint8_t> message)
{
    int bytesWritten = SSL_write(mSSL, message.data(), message.size());
    if (bytesWritten <= 0) {
        return Error(ErrorEnum::eRuntime, GetOpensslErrorString().c_str());
    }

    return ErrorEnum::eNone;
}

Error SecureChannel::Close()
{
    LOG_DBG() << "Close secure channel: port=" << mPort;

    if (!mConnected) {
        return ErrorEnum::eNone;
    }

    mConnected = false;

    if (mSSL) {
        SSL_shutdown(mSSL);
        SSL_free(mSSL);
        mSSL = nullptr;
    }

    if (mCtx) {
        SSL_CTX_free(mCtx);
        mCtx = nullptr;
    }

    mBioMethod.reset(nullptr);

    return mChannel->Close();
}

bool SecureChannel::IsConnected() const
{
    return mConnected;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

void SecureChannel::InitOpenssl()
{
    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();
}

void SecureChannel::CleanupOpenssl()
{
    EVP_cleanup();
}

std::string SecureChannel::GetOpensslErrorString()
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

RetWithError<EVP_PKEY*> SecureChannel::LoadPrivateKey(const std::string& keyURL)
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

SSL_CTX* SecureChannel::CreateSSLContext(const SSL_METHOD* method)
{
    SSL_CTX* ctx = SSL_CTX_new(method);
    if (!ctx) {
        throw std::runtime_error(GetOpensslErrorString());
    }

    return ctx;
}

Error SecureChannel::ConfigureSSLContext(SSL_CTX* ctx)
{
    LOG_DBG() << "Configuring SSL context";

    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);

    CertInfo certInfo;

    if (auto err = mCertProvider->GetCert(mCertStorage.c_str(), {}, {}, certInfo); !err.IsNone()) {
        return err;
    }

    auto [certificate, errLoad] = common::utils::LoadPEMCertificates(certInfo.mCertURL, *mCertLoader, *mCryptoProvider);
    if (!errLoad.IsNone()) {
        return errLoad;
    }

    auto [pkey, errLoadKey] = LoadPrivateKey(certInfo.mKeyURL.CStr());
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

    if (SSL_CTX_load_verify_locations(ctx, mCfg->mCACert.c_str(), nullptr) <= 0) {
        return Error(ErrorEnum::eRuntime, GetOpensslErrorString().c_str());
    }

    LOG_DBG() << "SSL context configured";

    return ErrorEnum::eNone;
}

int SecureChannel::CustomBIOWrite(BIO* bio, const char* buf, int len)
{
    SecureChannel*       channel = static_cast<SecureChannel*>(BIO_get_data(bio));
    std::vector<uint8_t> data(buf, buf + len);
    auto                 err = channel->mChannel->Write(std::move(data));

    return err.IsNone() ? len : -1;
}

int SecureChannel::CustomBIORead(BIO* bio, char* buf, int len)
{
    SecureChannel*       channel = static_cast<SecureChannel*>(BIO_get_data(bio));
    std::vector<uint8_t> data(len);

    if (auto err = channel->mChannel->Read(data); !err.IsNone()) {
        return -1;
    }

    std::memcpy(buf, data.data(), data.size());

    return data.size();
}

long SecureChannel::CustomBIOCtrl(
    [[maybe_unused]] BIO* bio, int cmd, [[maybe_unused]] long num, [[maybe_unused]] void* ptr)
{
    switch (cmd) {
    case BIO_CTRL_FLUSH:
        return 1;
    default:
        return 0;
    }
}

} // namespace aos::mp::communication
