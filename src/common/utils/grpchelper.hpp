/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GRPCHELPER_HPP_
#define GRPCHELPER_HPP_

#include <grpcpp/security/credentials.h>
#include <grpcpp/security/server_credentials.h>

#include <aos/common/crypto/crypto.hpp>
#include <aos/common/crypto/utils.hpp>
#include <aos/iam/certhandler.hpp>

namespace aos::common::utils {

/**
 * Get server credentials for mTLS.
 *
 * @param certInfo certificate information.
 * @param rootCertPath path to the root certificate.
 * @param certLoader certificate loader.
 * @param cryptoProvider crypto provider.
 * @return server credentials.
 */
std::shared_ptr<grpc::ServerCredentials> GetMTLSServerCredentials(const aos::iam::certhandler::CertInfo& certInfo,
    const aos::String& rootCertPath, aos::crypto::CertLoaderItf& certLoader,
    aos::crypto::x509::ProviderItf& cryptoProvider);

/**
 * Get server credentials for TLS.
 *
 * @param certInfo certificate information.
 * @param certLoader certificate loader.
 * @param cryptoProvider crypto provider.
 * @return server credentials.
 */
std::shared_ptr<grpc::ServerCredentials> GetTLSServerCredentials(const aos::iam::certhandler::CertInfo& certInfo,
    aos::crypto::CertLoaderItf& certLoader, aos::crypto::x509::ProviderItf& cryptoProvider);

/**
 * Get client credentials for MTLS connection.
 *
 * @param certInfo certificate information.
 * @param rootCertPath path to the root certificate.
 * @param certLoader certificate loader.
 * @param cryptoProvider crypto provider.
 * @return client credentials.
 */
std::shared_ptr<grpc::ChannelCredentials> GetMTLSClientCredentials(const aos::iam::certhandler::CertInfo& certInfo,
    const aos::String& rootCertPath, aos::crypto::CertLoaderItf& certLoader,
    aos::crypto::x509::ProviderItf& cryptoProvider);

/**
 * Get client credentials for TLS connection.
 *
 * @param rootCertPath path to the root certificate.
 * @return client credentials.
 */
std::shared_ptr<grpc::ChannelCredentials> GetTLSClientCredentials(const aos::String& rootCertPath);

} // namespace aos::common::utils

#endif
