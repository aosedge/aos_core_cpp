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

#include "utils/exception.hpp"
#include "utils/grpchelper.hpp"

using namespace aos;

/***********************************************************************************************************************
 * Statics
 **********************************************************************************************************************/

// LibP11 has its limitations on RFC7512 urls:
// https://www.rfc-editor.org/rfc/rfc7512.html
static std::string CreateLibP11PKCS11URL(const String& url)
{
    std::string result;

    try {
        // libp11 v0.4.11(provided with Ubuntu 22.04) loads pkcs11 objects with invalid id if label available.
        // Remove label to protect against loading invalid objects.
        std::regex objLabelRegex {"object=[^&?;]*[&?;]?"};

        result = std::regex_replace(url.CStr(), objLabelRegex, "");

        // libp11 doesn't process module-path
        std::regex modulePathRegex {"module\\-path=[^&?;]*[&?;]?"};

        result = std::regex_replace(result, modulePathRegex, "");
    } catch (const std::exception& exc) {
        AOS_ERROR_THROW(exc.what(), aos::ErrorEnum::eFailed);
    }

    return result;
}

static std::string CreateGRPCPKCS11URL(const String& keyURL)
{
    return "engine:pkcs11:" + CreateLibP11PKCS11URL(keyURL);
}

static std::string ConvertCertificateToPEM(
    const crypto::x509::Certificate& certificate, crypto::x509::ProviderItf& cryptoProvider)
{
    std::string result(crypto::cCertPEMLen, '0');
    String      view = result.c_str();

    auto err = cryptoProvider.X509CertToPEM(certificate, view);
    AOS_ERROR_CHECK_AND_THROW("Certificate conversion problem", err);

    result.resize(view.Size());

    return result;
}

static std::string ConvertCertificatesToPEM(
    const Array<crypto::x509::Certificate>& chain, crypto::x509::ProviderItf& cryptoProvider)
{
    std::string resultChain
        = std::accumulate(chain.begin(), chain.end(), std::string {}, [&](const std::string& result, const auto& cert) {
              return result + ConvertCertificateToPEM(cert, cryptoProvider);
          });

    return resultChain;
}

static std::shared_ptr<grpc::experimental::CertificateProviderInterface> GetMTLSCertificates(
    const iam::certhandler::CertInfo& certInfo, const String& rootCertPath, cryptoutils::CertLoaderItf& certLoader,
    crypto::x509::ProviderItf& cryptoProvider)
{
    auto [certificates, err] = certLoader.LoadCertsChainByURL(certInfo.mCertURL);
    AOS_ERROR_CHECK_AND_THROW("Load certificate by URL failed", err);

    std::ifstream file {rootCertPath.CStr()};
    std::string   rootCert((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    auto chain = Array<crypto::x509::Certificate>(certificates->begin(), certificates->Size());

    auto keyCertPair = grpc::experimental::IdentityKeyCertPair {
        CreateGRPCPKCS11URL(certInfo.mKeyURL), ConvertCertificatesToPEM(chain, cryptoProvider)};

    std::vector<grpc::experimental::IdentityKeyCertPair> keyCertPairs = {keyCertPair};

    return std::make_shared<grpc::experimental::StaticDataCertificateProvider>(rootCert, keyCertPairs);
}

static std::shared_ptr<grpc::experimental::CertificateProviderInterface> GetTLSServerCertificates(
    const iam::certhandler::CertInfo& certInfo, cryptoutils::CertLoaderItf& certLoader,
    crypto::x509::ProviderItf& cryptoProvider)
{
    auto [certificates, err] = certLoader.LoadCertsChainByURL(certInfo.mCertURL);

    AOS_ERROR_CHECK_AND_THROW("Load certificate by URL failed", err);

    if (certificates->Size() < 1) {
        throw std::runtime_error("Not expected number of certificates in the chain");
    }

    auto keyCertPair = grpc::experimental::IdentityKeyCertPair {
        CreateGRPCPKCS11URL(certInfo.mKeyURL), ConvertCertificatesToPEM(*certificates, cryptoProvider)};

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
    const String& rootCertPath, cryptoutils::CertLoaderItf& certLoader, crypto::x509::ProviderItf& cryptoProvider)
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
    cryptoutils::CertLoaderItf& certLoader, crypto::x509::ProviderItf& cryptoProvider)
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
    const String& rootCertPath, aos::cryptoutils::CertLoaderItf& certLoader,
    aos::crypto::x509::ProviderItf& cryptoProvider)
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
