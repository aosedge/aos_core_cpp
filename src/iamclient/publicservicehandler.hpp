/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PUBLICSERVICEHANDLER_HPP_
#define PUBLICSERVICEHANDLER_HPP_

#include <iamanager/v5/iamanager.grpc.pb.h>

#include "config/config.hpp"
#include "types.hpp"

namespace aos::mp::iamclient {

/**
 * Public service handler.
 */
class PublicServiceHandler {
public:
    /**
     * Initializes handler.
     *
     * @param cfg Configuration.
     * @param certLoader Certificate loader.
     * @param cryptoProvider Crypto provider.
     * @param insecureConnection Insecure connection.
     */
    Error Init(const config::Config& cfg, cryptoutils::CertLoaderItf& certLoader,
        crypto::x509::ProviderItf& cryptoProvider, bool insecureConnection, MTLSCredentialsFunc mtlsCredentialsFunc);

    /**
     * Gets MTLS configuration.
     *
     * @param certStorage Certificate storage.
     * @return MTLS configuration.
     */
    RetWithError<std::shared_ptr<grpc::ChannelCredentials>> GetMTLSConfig(const std::string& certStorage);

    /**
     * Gets TLS credentials.
     *
     * @return TLS credentials.
     */
    std::shared_ptr<grpc::ChannelCredentials> GetTLSCredentials();

    /**
     * Gets certificate.
     *
     * @param certType Certificate type.
     * @param certInfo Certificate info.
     * @return Error.
     */
    Error GetCertificate(const std::string& certType, iam::certhandler::CertInfo& certInfo);

private:
    constexpr static auto cIAMPublicServiceTimeout = std::chrono::seconds(10);

    using IAMPublicServicePtr = std::unique_ptr<iamanager::v5::IAMPublicService::Stub>;

    Error CreateCredentials(bool insecureConnection);

    const config::Config*                     mConfig;
    cryptoutils::CertLoaderItf*               mCertLoader;
    crypto::x509::ProviderItf*                mCryptoProvider;
    std::shared_ptr<grpc::ChannelCredentials> mCredentials;
    MTLSCredentialsFunc                       mMTLSCredentialsFunc;
};

} // namespace aos::mp::iamclient

#endif
