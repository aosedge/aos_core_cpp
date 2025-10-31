/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_IAMCLIENT_TLSCREDENTIALS_HPP_
#define AOS_COMMON_IAMCLIENT_TLSCREDENTIALS_HPP_

#include <functional>
#include <memory>
#include <string>

#include <common/utils/grpchelper.hpp>
#include <core/common/crypto/itf/certloader.hpp>
#include <core/common/crypto/itf/x509.hpp>
#include <core/common/iamclient/itf/certprovider.hpp>

#include "itf/tlscredentials.hpp"

namespace aos::common::iamclient {

/**
 * TLS credentials implementation.
 */
class TLSCredentials : public TLSCredentialsItf {
public:
    /**
     * Initializes TLS credentials.
     *
     * @param mtlsCredentialsFunc MTLS credentials function.
     * @return Error.
     */
    Error Init(const std::string& caCert, aos::iamclient::CertProviderItf& certProvider,
        crypto::CertLoaderItf& certLoader, crypto::x509::ProviderItf& cryptoProvider);

    /**
     * Gets MTLS configuration.
     *
     * @param certStorage Certificate storage.
     * @param insecureConnection If true, returns insecure credentials.
     * @return MTLS credentials.
     */
    RetWithError<std::shared_ptr<grpc::ChannelCredentials>> GetMTLSClientCredentials(
        const String& certStorage, bool insecureConnection) override;

    /**
     * Gets TLS credentials.
     *
     * @param insecureConnection If true, returns insecure credentials.
     * @return TLS credentials.
     */
    RetWithError<std::shared_ptr<grpc::ChannelCredentials>> GetTLSClientCredentials(bool insecureConnection) override;

private:
    aos::iamclient::CertProviderItf* mCertProvider {};
    crypto::CertLoaderItf*           mCertLoader {};
    crypto::x509::ProviderItf*       mCryptoProvider {};
    std::string                      mCACert;
};

} // namespace aos::common::iamclient

#endif
