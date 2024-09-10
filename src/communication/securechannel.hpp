/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SECURECHANNEL_HPP_
#define SECURECHANNEL_HPP_

#include <openssl/engine.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

#include <aos/common/cryptoutils.hpp>

#include "config/config.hpp"
#include "iamclient/types.hpp"
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
     */
    SecureChannel(const config::Config& cfg, CommChannelItf& channel, iamclient::CertProviderItf& certProvider,
        cryptoutils::CertLoaderItf& certLoader, crypto::x509::ProviderItf& cryptoProvider, int port,
        const std::string& certStorage);

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

private:
    void        InitOpenssl();
    void        CleanupOpenssl();
    SSL_CTX*    CreateSSLContext(const SSL_METHOD* method);
    Error       ConfigureSSLContext(SSL_CTX* ctx, ENGINE* eng);
    static int  CustomBIOWrite(BIO* bio, const char* buf, int len);
    static int  CustomBIORead(BIO* bio, char* buf, int len);
    static long CustomBIOCtrl(BIO* bio, int cmd, long num, void* ptr);
    BIO_METHOD* CreateCustomBIOMethod();
    std::string GetOpensslErrorString();

    CommChannelItf*             mChannel {};
    iamclient::CertProviderItf* mCertProvider {};
    cryptoutils::CertLoaderItf* mCertLoader {};
    crypto::x509::ProviderItf*  mCryptoProvider {};
    const config::Config*       mCfg {};
    int                         mPort {};
    std::string                 mCertStorage {};
    SSL_CTX*                    mCtx {};
    SSL*                        mSsl {};
};

} // namespace aos::mp::communication

#endif /* SECURECHANNEL_HPP_ */
