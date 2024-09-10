/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IAM_TYPES_HPP_
#define IAM_TYPES_HPP_

#include <functional>
#include <memory>

#include <grpcpp/security/credentials.h>

#include <iamanager/v5/iamanager.grpc.pb.h>

#include <aos/common/crypto.hpp>
#include <aos/common/cryptoutils.hpp>
#include <aos/common/tools/error.hpp>
#include <aos/iam/certhandler.hpp>

namespace aos::mp::iamclient {

/**
 * MTLS credentials function.
 */

using MTLSCredentialsFunc = std::function<std::shared_ptr<grpc::ChannelCredentials>(
    const iam::certhandler::CertInfo&, const String&, cryptoutils::CertLoaderItf&, crypto::x509::ProviderItf&)>;

/**
 * Certificate provider interface.
 */
class CertProviderItf {
public:
    /**
     * Destructor.
     */
    virtual ~CertProviderItf() = default;

    /**
     * Gets MTLS configuration.
     *
     * @param certStorage Certificate storage.
     * @return MTLS configuration.
     */
    virtual RetWithError<std::shared_ptr<grpc::ChannelCredentials>> GetMTLSConfig(const std::string& certStorage) = 0;

    /**
     * Gets TLS credentials.
     *
     * @return TLS credentials.
     */
    virtual std::shared_ptr<grpc::ChannelCredentials> GetTLSCredentials() = 0;

    /**
     * Gets certificate.
     *
     * @param certType Certificate type.
     * @param certInfo Certificate info.
     * @return Error.
     */
    virtual Error GetCertificate(const std::string& certType, iam::certhandler::CertInfo& certInfo) = 0;
};

} // namespace aos::mp::iamclient

#endif
