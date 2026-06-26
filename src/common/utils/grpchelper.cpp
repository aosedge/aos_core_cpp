/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <chrono>
#include <fstream>
#include <grpc/grpc.h>
#include <numeric>
#include <regex>
#include <streambuf>

#include "cryptohelper.hpp"
#include "exception.hpp"
#include "grpchelper.hpp"
#include "pkcs11helper.hpp"

using namespace aos;

namespace {

constexpr auto cGrpcClientKeepAliveTime = 10 * Time::cSeconds;

} // namespace

/***********************************************************************************************************************
 * Statics
 **********************************************************************************************************************/

static std::string CreateGRPCPKCS11PrivKeyURL(const String& keyURL)
{
    auto [libP11URL, createErr] = aos::common::utils::CreatePKCS11URL(keyURL);
    AOS_ERROR_CHECK_AND_THROW(createErr, "failed to create PKCS11 URL");

    auto [pem, encodeErr] = aos::common::utils::PEMEncodePKCS11URL(libP11URL);
    AOS_ERROR_CHECK_AND_THROW(encodeErr, "failed to encode PKCS11 URL");

    return pem;
}

static std::shared_ptr<grpc::experimental::CertificateProviderInterface> GetMTLSCertificates(const CertInfo& certInfo,
    const String& rootCertPath, crypto::CertLoaderItf& certLoader, crypto::x509::ProviderItf& cryptoProvider)
{
    auto [certificates, err] = aos::common::utils::LoadPEMCertificates(certInfo.mCertURL, certLoader, cryptoProvider);
    AOS_ERROR_CHECK_AND_THROW(err, "load certificate by URL failed");

    std::ifstream file {rootCertPath.CStr()};
    if (!file.is_open()) {
        AOS_ERROR_THROW(ErrorEnum::eNotFound, "failed to open root certificate file");
    }

    std::string rootCert((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (rootCert.empty()) {
        AOS_ERROR_THROW(ErrorEnum::eInvalidArgument, "root certificate file is empty");
    }

    auto keyCertPair
        = grpc::experimental::IdentityKeyCertPair {CreateGRPCPKCS11PrivKeyURL(certInfo.mKeyURL), certificates};

    std::vector<grpc::experimental::IdentityKeyCertPair> keyCertPairs = {keyCertPair};

    return std::make_shared<grpc::experimental::StaticDataCertificateProvider>(rootCert, keyCertPairs);
}

static std::shared_ptr<grpc::experimental::CertificateProviderInterface> GetTLSServerCertificates(
    const CertInfo& certInfo, crypto::CertLoaderItf& certLoader, crypto::x509::ProviderItf& cryptoProvider)
{
    auto [certificates, err] = aos::common::utils::LoadPEMCertificates(certInfo.mCertURL, certLoader, cryptoProvider);
    AOS_ERROR_CHECK_AND_THROW(err, "Load certificate by URL failed");

    auto keyCertPair
        = grpc::experimental::IdentityKeyCertPair {CreateGRPCPKCS11PrivKeyURL(certInfo.mKeyURL), certificates};

    std::vector<grpc::experimental::IdentityKeyCertPair> keyCertPairs = {keyCertPair};

    return std::make_shared<grpc::experimental::StaticDataCertificateProvider>("", keyCertPairs);
}

static std::shared_ptr<grpc::experimental::CertificateProviderInterface> GetTLSClientCertificates(
    const String& rootCertPath)
{
    std::ifstream file {rootCertPath.CStr()};
    if (!file.is_open()) {
        AOS_ERROR_THROW(ErrorEnum::eNotFound, "failed to open root certificate file");
    }

    std::string rootCert((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (rootCert.empty()) {
        AOS_ERROR_THROW(ErrorEnum::eInvalidArgument, "root certificate file is empty");
    }

    return std::make_shared<grpc::experimental::StaticDataCertificateProvider>(
        rootCert, std::vector<grpc::experimental::IdentityKeyCertPair> {});
}

/***********************************************************************************************************************
 * Public interface
 **********************************************************************************************************************/

namespace aos::common::utils {

std::shared_ptr<grpc::ServerCredentials> GetMTLSServerCredentials(const CertInfo& certInfo, const String& rootCertPath,
    crypto::CertLoaderItf& certLoader, crypto::x509::ProviderItf& cryptoProvider)
{
    auto certificates = GetMTLSCertificates(certInfo, rootCertPath, certLoader, cryptoProvider);

    grpc::experimental::TlsServerCredentialsOptions options {certificates};

    options.set_cert_request_type(GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
    options.set_check_call_host(false);
    options.watch_root_certs();
    options.watch_identity_key_cert_pairs();
    options.set_root_cert_name("root");
    options.set_identity_cert_name("identity");

    return grpc::experimental::TlsServerCredentials(options);
}

std::shared_ptr<grpc::ServerCredentials> GetTLSServerCredentials(
    const CertInfo& certInfo, crypto::CertLoaderItf& certLoader, crypto::x509::ProviderItf& cryptoProvider)
{
    auto certificates = GetTLSServerCertificates(certInfo, certLoader, cryptoProvider);

    grpc::experimental::TlsServerCredentialsOptions options {certificates};

    options.set_cert_request_type(GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE);
    options.set_check_call_host(false);
    options.watch_identity_key_cert_pairs();
    options.set_identity_cert_name("identity");

    return grpc::experimental::TlsServerCredentials(options);
}

std::shared_ptr<grpc::ChannelCredentials> GetMTLSClientCredentials(const CertInfo& certInfo, const String& rootCertPath,
    crypto::CertLoaderItf& certLoader, crypto::x509::ProviderItf& cryptoProvider)
{
    auto certificates = GetMTLSCertificates(certInfo, rootCertPath, certLoader, cryptoProvider);

    grpc::experimental::TlsChannelCredentialsOptions options;
    options.set_certificate_provider(certificates);
    options.set_verify_server_certs(true);

    options.set_check_call_host(false);
    options.watch_root_certs();
    options.set_root_cert_name("root");
    options.watch_identity_key_cert_pairs();
    options.set_identity_cert_name("identity");

    return grpc::experimental::TlsCredentials(options);
}

std::shared_ptr<grpc::ChannelCredentials> GetTLSClientCredentials(const aos::String& rootCertPath)
{
    auto certificates = GetTLSClientCertificates(rootCertPath);

    grpc::experimental::TlsChannelCredentialsOptions options;
    options.set_certificate_provider(certificates);
    options.set_verify_server_certs(true);

    options.set_check_call_host(false);
    options.watch_root_certs();
    options.set_root_cert_name("root");

    return grpc::experimental::TlsCredentials(options);
}

grpc::ChannelArguments CreateGRPCChannelArguments()
{
    constexpr auto cKeepAliveTimeout        = 3 * Time::cSeconds;
    constexpr auto cMinReconnectBackoff     = 1 * Time::cSeconds;
    constexpr auto cInitialReconnectBackoff = 1 * Time::cSeconds;
    constexpr auto cMaxReconnectBackoff     = 3 * Time::cSeconds;

    grpc::ChannelArguments args;

    // Default: disabled for clients (no periodic keepalive pings).
    args.SetInt(GRPC_ARG_KEEPALIVE_TIME_MS, static_cast<int>(cGrpcClientKeepAliveTime.Milliseconds()));
    // Default: 20 seconds.
    args.SetInt(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, static_cast<int>(cKeepAliveTimeout.Milliseconds()));
    // Default: 0 (false).
    args.SetInt(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS, 1);
    // Default: 2.
    args.SetInt(GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA, 0);
    // Default: 1 second.
    args.SetInt(GRPC_ARG_INITIAL_RECONNECT_BACKOFF_MS, static_cast<int>(cInitialReconnectBackoff.Milliseconds()));
    // Default: 20 seconds.
    args.SetInt(GRPC_ARG_MIN_RECONNECT_BACKOFF_MS, static_cast<int>(cMinReconnectBackoff.Milliseconds()));
    // Default: 120 seconds.
    args.SetInt(GRPC_ARG_MAX_RECONNECT_BACKOFF_MS, static_cast<int>(cMaxReconnectBackoff.Milliseconds()));

    return args;
}

void SetGRPCServerOptions(grpc::ServerBuilder& builder)
{
    const auto keepAlivePingMs = static_cast<int>(cGrpcClientKeepAliveTime.Milliseconds());

    // Client uses GRPC_ARG_KEEPALIVE_TIME_MS = keepAlivePingMs; server default min interval between
    // PINGs without data is much larger, which triggers ENHANCE_YOUR_CALM / too_many_pings GOAWAY.
    (void)builder.AddChannelArgument(GRPC_ARG_HTTP2_MIN_RECV_PING_INTERVAL_WITHOUT_DATA_MS, keepAlivePingMs);
    // Disable ping strike mechanism to avoid GOAWAY due to timing jitter when client and server
    // keepalive intervals are equal.
    (void)builder.AddChannelArgument(GRPC_ARG_HTTP2_MAX_PING_STRIKES, 0);
}

bool WaitForChannelReady(const std::shared_ptr<grpc::Channel>& channel, std::chrono::milliseconds timeout,
    const std::function<bool()>& shouldStop)
{
    if (!channel) {
        return false;
    }

    constexpr auto cPollInterval = std::chrono::milliseconds(100);

    const auto deadline = std::chrono::system_clock::now() + timeout;

    while (true) {
        if (shouldStop && shouldStop()) {
            return false;
        }

        const auto state = channel->GetState(/*try_to_connect=*/true);

        if (state == GRPC_CHANNEL_READY) {
            return true;
        }

        if (state == GRPC_CHANNEL_SHUTDOWN) {
            return false;
        }

        const auto now = std::chrono::system_clock::now();
        if (now >= deadline) {
            return false;
        }

        const auto pollDeadline = std::min(now + cPollInterval, deadline);

        channel->WaitForStateChange(state, pollDeadline);
    }
}

} // namespace aos::common::utils
