/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_UTILS_GRPCHELPER_HPP_
#define AOS_COMMON_UTILS_GRPCHELPER_HPP_

#include <chrono>
#include <functional>
#include <memory>

#include <grpcpp/channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server_builder.h>

#include <core/common/crypto/itf/certloader.hpp>
#include <core/common/crypto/itf/crypto.hpp>
#include <core/iam/certhandler/certhandler.hpp>

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
std::shared_ptr<grpc::ServerCredentials> GetMTLSServerCredentials(const CertInfo& certInfo,
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
std::shared_ptr<grpc::ServerCredentials> GetTLSServerCredentials(
    const CertInfo& certInfo, aos::crypto::CertLoaderItf& certLoader, aos::crypto::x509::ProviderItf& cryptoProvider);

/**
 * Get client credentials for MTLS connection.
 *
 * @param certInfo certificate information.
 * @param rootCertPath path to the root certificate.
 * @param certLoader certificate loader.
 * @param cryptoProvider crypto provider.
 * @return client credentials.
 */
std::shared_ptr<grpc::ChannelCredentials> GetMTLSClientCredentials(const CertInfo& certInfo,
    const aos::String& rootCertPath, aos::crypto::CertLoaderItf& certLoader,
    aos::crypto::x509::ProviderItf& cryptoProvider);

/**
 * Get client credentials for TLS connection.
 *
 * @param rootCertPath path to the root certificate.
 * @return client credentials.
 */
std::shared_ptr<grpc::ChannelCredentials> GetTLSClientCredentials(const aos::String& rootCertPath);

/**
 * Create common gRPC channel arguments for clients.
 *
 * @return channel arguments.
 */
grpc::ChannelArguments CreateGRPCChannelArguments();

/**
 * Sets server channel arguments so HTTP/2 PING / keepalive from clients matches CreateGRPCChannelArguments()
 * (avoids GOAWAY ENHANCE_YOUR_CALM "too_many_pings").
 *
 * @param builder server builder.
 */
void SetGRPCServerOptions(grpc::ServerBuilder& builder);

/**
 * Waits until the channel reaches GRPC_CHANNEL_READY state. Triggers a connection attempt
 * (TCP connect + TLS handshake) if the channel is idle. Used to verify that the credentials
 * configured on the channel match what the server expects, before committing to long-lived
 * RPCs that would otherwise fail lazily inside Read/Write.
 *
 * The optional shouldStop predicate is polled periodically; returning true aborts the wait
 * early so callers can be unblocked by external stop signals.
 *
 * @param channel channel to probe.
 * @param timeout maximum time to wait for the channel to become ready.
 * @param shouldStop optional cancellation predicate, polled between state checks.
 * @return true if the channel reached READY within the timeout, false on timeout, shutdown
 *         or cancellation.
 */
bool WaitForChannelReady(const std::shared_ptr<grpc::Channel>& channel, std::chrono::milliseconds timeout,
    const std::function<bool()>& shouldStop = {});

} // namespace aos::common::utils

#endif
