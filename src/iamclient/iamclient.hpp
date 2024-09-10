/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IAMCLIENT_HPP_
#define IAMCLIENT_HPP_

#include <memory>
#include <optional>
#include <string>

#include <utils/grpchelper.hpp>

#include "config/config.hpp"
#include "publicnodeclient.hpp"
#include "publicservicehandler.hpp"
#include "types.hpp"

namespace aos::mp::iamclient {

/**
 * IAM client.
 */
class IAMClient : public CertProviderItf {
public:
    IAMClient() = default;
    /**
     * Initializes IAM client.
     *
     * @param cfg Configuration.
     * @param certLoader Certificate loader.
     * @param cryptoProvider Crypto provider.
     * @param channel Channel.
     * @param provisioningMode Provisioning mode.
     * @param mtlsCredentialsFunc MTLS credentials function.
     * @return Error.
     */
    Error Init(const config::Config& cfg, cryptoutils::CertLoaderItf& certLoader,
        crypto::x509::ProviderItf& cryptoProvider, bool provisioningMode = false,
        MTLSCredentialsFunc mtlsCredentialsFunc = common::utils::GetMTLSClientCredentials);

    /**
     * Gets MTLS configuration.
     *
     * @param certStorage Certificate storage.
     * @return MTLS configuration.
     */
    RetWithError<std::shared_ptr<grpc::ChannelCredentials>> GetMTLSConfig(const std::string& certStorage) override;

    /**
     * Gets TLS credentials.
     *
     * @return TLS credentials.
     */
    std::shared_ptr<grpc::ChannelCredentials> GetTLSCredentials() override;

    /**
     * Gets certificate.
     *
     * @param certType Certificate type.
     * @param certInfo Certificate information.
     * @return Error.
     */
    Error GetCertificate(const std::string& certType, iam::certhandler::CertInfo& certInfo) override;

    /**
     * Gets public handler.
     *
     * @return Public handler.
     */
    communication::HandlerItf& GetPublicHandler() { return mPublicNodeClient.value(); }

    /**
     * Gets protected handler.
     *
     * @return Protected handler.
     */
    communication::HandlerItf& GetProtectedHandler() { return mProtectedNodeClient.value(); }

private:
    std::optional<PublicServiceHandler> mPublicServiceHandler;
    std::optional<PublicNodeClient>     mPublicNodeClient;
    std::optional<PublicNodeClient>     mProtectedNodeClient;
};

} // namespace aos::mp::iamclient

#endif
