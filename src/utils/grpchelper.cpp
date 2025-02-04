/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <fstream>
#include <numeric>
#include <regex>
#include <streambuf>

#include "utils/cryptohelper.hpp"
#include "utils/exception.hpp"
#include "utils/grpchelper.hpp"
#include "utils/pkcs11helper.hpp"

using namespace aos;

/***********************************************************************************************************************
 * Statics
 **********************************************************************************************************************/

static std::string CreateGRPCPKCS11URL(const String& keyURL)
{
    auto [libP11URL, err] = aos::common::utils::CreatePKCS11URL(keyURL);
    AOS_ERROR_CHECK_AND_THROW("failed to create PKCS11 URL", err);

    return "engine:pkcs11:" + libP11URL;
}

static std::shared_ptr<grpc::experimental::CertificateProviderInterface> GetMTLSCertificates(
    const iam::certhandler::CertInfo& certInfo, const String& rootCertPath, crypto::CertLoaderItf& certLoader,
    crypto::x509::ProviderItf& cryptoProvider)
{
    auto [certificates, err] = aos::common::utils::LoadPEMCertificates(certInfo.mCertURL, certLoader, cryptoProvider);
    AOS_ERROR_CHECK_AND_THROW("load certificate by URL failed", err);

    std::ifstream file {rootCertPath.CStr()};
    std::string   rootCert((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    auto keyCertPair = grpc::experimental::IdentityKeyCertPair {CreateGRPCPKCS11URL(certInfo.mKeyURL), certificates};

    std::vector<grpc::experimental::IdentityKeyCertPair> keyCertPairs = {keyCertPair};

    return std::make_shared<grpc::experimental::StaticDataCertificateProvider>(rootCert, keyCertPairs);
}

static std::shared_ptr<grpc::experimental::CertificateProviderInterface> GetTLSServerCertificates(
    const iam::certhandler::CertInfo& certInfo, crypto::CertLoaderItf& certLoader,
    crypto::x509::ProviderItf& cryptoProvider)
{
    auto [certificates, err] = aos::common::utils::LoadPEMCertificates(certInfo.mCertURL, certLoader, cryptoProvider);
    AOS_ERROR_CHECK_AND_THROW("Load certificate by URL failed", err);

    auto keyCertPair = grpc::experimental::IdentityKeyCertPair {CreateGRPCPKCS11URL(certInfo.mKeyURL), certificates};

    std::vector<grpc::experimental::IdentityKeyCertPair> keyCertPairs = {keyCertPair};

    return std::make_shared<grpc::experimental::StaticDataCertificateProvider>("", keyCertPairs);
}

static std::shared_ptr<grpc::experimental::CertificateProviderInterface> GetTLSClientCertificates(
    const String& rootCertPath)
{
    std::ifstream file {rootCertPath.CStr()};
    std::string   rootCert((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    return std::make_shared<grpc::experimental::StaticDataCertificateProvider>(
        rootCert, std::vector<grpc::experimental::IdentityKeyCertPair> {});
}

/***********************************************************************************************************************
 * Public interface
 **********************************************************************************************************************/

namespace aos::common::utils {

std::shared_ptr<grpc::ServerCredentials> GetMTLSServerCredentials(const iam::certhandler::CertInfo& certInfo,
    const String& rootCertPath, crypto::CertLoaderItf& certLoader, crypto::x509::ProviderItf& cryptoProvider)
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

std::shared_ptr<grpc::ServerCredentials> GetTLSServerCredentials(const iam::certhandler::CertInfo& certInfo,
    crypto::CertLoaderItf& certLoader, crypto::x509::ProviderItf& cryptoProvider)
{
    auto certificates = GetTLSServerCertificates(certInfo, certLoader, cryptoProvider);

    grpc::experimental::TlsServerCredentialsOptions options {certificates};

    options.set_cert_request_type(GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE);
    options.set_check_call_host(false);
    options.watch_identity_key_cert_pairs();
    options.set_identity_cert_name("identity");

    return grpc::experimental::TlsServerCredentials(options);
}

std::shared_ptr<grpc::ChannelCredentials> GetMTLSClientCredentials(const aos::iam::certhandler::CertInfo& certInfo,
    const String& rootCertPath, aos::crypto::CertLoaderItf& certLoader, aos::crypto::x509::ProviderItf& cryptoProvider)
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

} // namespace aos::common::utils
