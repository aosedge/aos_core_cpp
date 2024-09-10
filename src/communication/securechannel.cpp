
/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
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

#include <utils/cryptohelper.hpp>
#include <utils/pkcs11helper.hpp>

#include "logger/logmodule.hpp"
#include "securechannel.hpp"

namespace aos::mp::communication {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

SecureChannel::SecureChannel(const config::Config& cfg, CommChannelItf& channel,
    iamclient::CertProviderItf& certProvider, cryptoutils::CertLoaderItf& certLoader,
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

    ENGINE* eng = ENGINE_by_id("pkcs11");
    if (!eng) {
        throw std::runtime_error("failed to load PKCS11 engine");
    }

    if (!ENGINE_init(eng)) {
        throw std::runtime_error("failed to initialize PKCS11 engine");
    }

    const SSL_METHOD* method = TLS_server_method();
    mCtx                     = CreateSSLContext(method);

    if (auto err = ConfigureSSLContext(mCtx, eng); !err.IsNone()) {
        SSL_CTX_free(mCtx);
        CleanupOpenssl();

        throw std::runtime_error(err.Message());
    }
}

SecureChannel::~SecureChannel()
{
    LOG_DBG() << "Destroy secure channel: port=" << mPort;

    SSL_free(mSsl);
    SSL_CTX_free(mCtx);
    CleanupOpenssl();
}

Error SecureChannel::Connect()
{
    LOG_DBG() << "Connect to secure channel: port=" << mPort;

    if (auto err = mChannel->Connect(); !err.IsNone()) {
        return err;
    }

    if (mSsl != nullptr) {
        SSL_free(mSsl);
    }

    mSsl               = SSL_new(mCtx);
    BIO_METHOD* method = CreateCustomBIOMethod();
    BIO*        rbio   = BIO_new(method);
    BIO*        wbio   = BIO_new(method);
    BIO_set_data(rbio, this);
    BIO_set_data(wbio, this);
    SSL_set_bio(mSsl, rbio, wbio);

    if (SSL_accept(mSsl) <= 0) {
        LOG_ERR() << "Failed to accept SSL connection";

        return Error(ErrorEnum::eRuntime, GetOpensslErrorString().c_str());
    }

    LOG_DBG() << "SSL connection accepted";

    return ErrorEnum::eNone;
}

Error SecureChannel::Read(std::vector<uint8_t>& message)
{
    if (message.empty()) {
        return Error(ErrorEnum::eRuntime, "message buffer is empty");
    }

    LOG_DBG() << "Requesting secure read: port=" << mPort << ", size=" << message.size();

    int bytes_read = SSL_read(mSsl, message.data(), message.size());
    if (bytes_read <= 0) {
        return Error(ErrorEnum::eRuntime, GetOpensslErrorString().c_str());
    }

    return ErrorEnum::eNone;
}

Error SecureChannel::Write(std::vector<uint8_t> message)
{
    LOG_DBG() << "Write secure data port=" << mPort << ", size=" << message.size();

    if (int bytes_written = SSL_write(mSsl, message.data(), message.size()); bytes_written <= 0) {
        return Error(ErrorEnum::eRuntime, GetOpensslErrorString().c_str());
    }

    return ErrorEnum::eNone;
}

Error SecureChannel::Close()
{
    LOG_DBG() << "Close secure channel: port=" << mPort;

    auto err = mChannel->Close();

    if (mSsl != nullptr) {
        SSL_shutdown(mSsl);
    }

    return err;
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

SSL_CTX* SecureChannel::CreateSSLContext(const SSL_METHOD* method)
{
    SSL_CTX* ctx = SSL_CTX_new(method);
    if (!ctx) {
        throw std::runtime_error(GetOpensslErrorString());
    }

    return ctx;
}

Error SecureChannel::ConfigureSSLContext(SSL_CTX* ctx, ENGINE* eng)
{
    LOG_DBG() << "Configuring SSL context";

    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);

    iam::certhandler::CertInfo certInfo;

    auto err = mCertProvider->GetCertificate(mCertStorage, certInfo);
    if (!err.IsNone()) {
        return err;
    }

    auto [certificate, errLoad] = common::utils::LoadPEMCertificates(certInfo.mCertURL, *mCertLoader, *mCryptoProvider);
    if (!errLoad.IsNone()) {
        return errLoad;
    }

    auto [keyURI, errURI] = common::utils::CreatePKCS11URL(certInfo.mKeyURL);
    if (!errURI.IsNone()) {
        return errURI;
    }

    EVP_PKEY* pkey = ENGINE_load_private_key(eng, keyURI.c_str(), nullptr, nullptr);
    if (!pkey) {
        return Error(ErrorEnum::eRuntime, GetOpensslErrorString().c_str());
    }

    if (SSL_CTX_use_PrivateKey(ctx, pkey) <= 0) {
        return Error(ErrorEnum::eRuntime, GetOpensslErrorString().c_str());
    }

    // Split the certificate chain into individual certificates
    BIO* bio = BIO_new_mem_buf(certificate.c_str(), -1);
    if (!bio) {
        return Error(ErrorEnum::eRuntime, "failed to create BIO");
    }

    std::unique_ptr<BIO, decltype(&BIO_free)> bioPtr(bio, BIO_free);

    X509* cert = PEM_read_bio_X509(bio, nullptr, 0, nullptr);
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

    while ((intermediateCert = PEM_read_bio_X509(bio, nullptr, 0, nullptr)) != nullptr) {
        sk_X509_push(chain.get(), intermediateCert);
    }

    if (SSL_CTX_set1_chain(ctx, chain.get()) <= 0) {
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
    LOG_DBG() << "Write to the secure channel: expectedSize=" << len;

    SecureChannel*       channel = static_cast<SecureChannel*>(BIO_get_data(bio));
    std::vector<uint8_t> data(buf, buf + len);
    auto                 err = channel->mChannel->Write(std::move(data));

    return err.IsNone() ? len : -1;
}

int SecureChannel::CustomBIORead(BIO* bio, char* buf, int len)
{
    LOG_DBG() << "Read from the secure channel: expectedSize=" << len;

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

BIO_METHOD* SecureChannel::CreateCustomBIOMethod()
{
    BIO_METHOD* method = BIO_meth_new(BIO_TYPE_SOURCE_SINK, "Custom BIO");

    BIO_meth_set_write(method, CustomBIOWrite);
    BIO_meth_set_read(method, CustomBIORead);
    BIO_meth_set_ctrl(method, CustomBIOCtrl);

    return method;
}

} // namespace aos::mp::communication
