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

#include "itf/publicservice.hpp"
#include "itf/tlscredentials.hpp"

namespace aos::common::iamclient {
/**
 * MTLS credentials function.
 */
using MTLSCredentialsFunc
    = std::function<std::shared_ptr<grpc::ChannelCredentials>(const iam::certhandler::CertInfo& certInfo,
        const String& rootCA, crypto::CertLoaderItf& certLoader, crypto::x509::ProviderItf& cryptoProvider)>;

/**
 * TLS credentials implementation.
 */
class TLSCredentials : public TLSCredentialsItf {
public:
    /**
     * Initializes.
     *
     * @param mtlsCredentialsFunc MTLS credentials function.
     * @return Error.
     */
    Error Init(const std::string& caCert, PublicServiceItf& publicservice, crypto::CertLoaderItf& certLoader,
        crypto::x509::ProviderItf& cryptoProvider,
        MTLSCredentialsFunc        mtlsCredentialsFunc = common::utils::GetMTLSClientCredentials);

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
     * @return TLS credentials.
     */
    RetWithError<std::shared_ptr<grpc::ChannelCredentials>> GetTLSClientCredentials() override;

private:
    PublicServiceItf*          mPublicService {};
    crypto::CertLoaderItf*     mCertLoader {};
    crypto::x509::ProviderItf* mCryptoProvider {};
    MTLSCredentialsFunc        mMTLSCredentialsFunc;
    std::string                mCACert;
};

} // namespace aos::common::iamclient

#endif
