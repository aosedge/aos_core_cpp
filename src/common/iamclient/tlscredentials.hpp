/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_IAMCLIENT_TLSCREDENTIALS_HPP_
#define AOS_COMMON_IAMCLIENT_TLSCREDENTIALS_HPP_

#include <memory>

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
     * @param certProvider certificate provider.
     * @param certLoader certificate loader.
     * @param cryptoProvider crypto provider.
     * @return Error.
     */
    Error Init(aos::iamclient::CertProviderItf& certProvider, crypto::CertLoaderItf& certLoader,
        crypto::x509::ProviderItf& cryptoProvider);

    /**
     * Gets MTLS configuration.
     *
     * @param certStorage Certificate storage.
     * @return MTLS credentials.
     */
    RetWithError<std::shared_ptr<grpc::ChannelCredentials>> GetMTLSClientCredentials(
        const String& certStorage) override;

    /**
     * Gets TLS credentials.
     *
     * @param certStorage Certificate storage.
     * @return TLS credentials.
     */
    RetWithError<std::shared_ptr<grpc::ChannelCredentials>> GetTLSClientCredentials(const String& certStorage) override;

private:
    aos::iamclient::CertProviderItf* mCertProvider {};
    crypto::CertLoaderItf*           mCertLoader {};
    crypto::x509::ProviderItf*       mCryptoProvider {};
};

} // namespace aos::common::iamclient

#endif
