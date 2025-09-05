/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_MP_COMMUNICATION_SECURECHANNEL_HPP_
#define AOS_MP_COMMUNICATION_SECURECHANNEL_HPP_

#include <atomic>
#include <memory>
#include <openssl/err.h>
#include <openssl/ssl.h>

#include <core/common/crypto/cryptoutils.hpp>

#include <common/iamclient/publicservicehandler.hpp>
#include <mp/config/config.hpp>

#include "types.hpp"

namespace aos::mp::communication {

/**
 * Secure Channel class.
 */
class SecureChannel : public CommChannelItf {
public:
    /**
     * Constructor.
     *
     * @param cfg Configuration.
     * @param channel Communication channel.
     * @param certProvider Certificate provider.
     * @param certLoader Certificate loader.
     * @param cryptoProvider Crypto provider.
     * @param port Port.
     * @param certStorage Certificate storage path.
     */
    SecureChannel(const config::Config& cfg, CommChannelItf& channel,
        common::iamclient::TLSCredentialsItf& certProvider, crypto::CertLoaderItf& certLoader,
        crypto::x509::ProviderItf& cryptoProvider, int port, const std::string& certStorage);

    /**
     * Destructor.
     */
    ~SecureChannel();

    /**
     * Connects to channel.
     *
     * @return Error.
     */
    Error Connect() override;

    /**
     * Reads message.
     *
     * @param message Message.
     * @return Error.
     */
    Error Read(std::vector<uint8_t>& message) override;

    /**
     * Writes message.
     *
     * @param message Message.
     * @return Error.
     */
    Error Write(std::vector<uint8_t> message) override;

    /**
     * Closes channel.
     *
     * @return Error.
     */
    Error Close() override;

    /**
     * Checks if channel is connected.
     *
     * @return bool.
     */
    bool IsConnected() const override;

private:
    static int  CustomBIOWrite(BIO* bio, const char* buf, int len);
    static int  CustomBIORead(BIO* bio, char* buf, int len);
    static long CustomBIOCtrl(BIO* bio, int cmd, long num, void* ptr);

    void     InitOpenssl();
    void     CleanupOpenssl();
    SSL_CTX* CreateSSLContext(const SSL_METHOD* method);
    Error    ConfigureSSLContext(SSL_CTX* ctx);

    CommChannelItf*                       mChannel {};
    common::iamclient::TLSCredentialsItf* mCertProvider {};
    crypto::CertLoaderItf*                mCertLoader {};
    crypto::x509::ProviderItf*            mCryptoProvider {};
    const config::Config*                 mCfg {};
    int                                   mPort {};
    std::string                           mCertStorage;

    SSL_CTX*                                              mCtx {};
    SSL*                                                  mSSL {};
    std::unique_ptr<BIO_METHOD, decltype(&BIO_meth_free)> mBioMethod {nullptr, BIO_meth_free};
    std::atomic<bool>                                     mConnected {};
};

/**
 * Loads private key from AOS URL and returns EVP_PKEY pointer.
 *
 * @param keyURL key URL.
 * @return RetWithError<EVP_PKEY*>.
 */
RetWithError<EVP_PKEY*> LoadPrivateKey(const std::string& keyURL);

} // namespace aos::mp::communication

#endif
