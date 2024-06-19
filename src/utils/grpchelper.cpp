/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <fstream>
#include <numeric>
#include <streambuf>

#include "utils/exception.hpp"
#include "utils/grpchelper.hpp"

using namespace aos;

/***********************************************************************************************************************
 * Statics
 **********************************************************************************************************************/

// The PKCS #11 URI Scheme: https://www.rfc-editor.org/rfc/rfc7512.html
static std::string CreateRFC7512URL(
    const String& token, const String& label, const Array<uint8_t>& id, const String& userPin)
{
    const auto addParam = [](const char* name, const char* param, bool opaque, std::string& paramList) {
        if (!paramList.empty()) {
            const char* delim = opaque ? ";" : "&";
            paramList.append(delim);
        }

        paramList += std::string(name) + "=" + param;
    };

    std::string                     opaque, query;
    StaticString<pkcs11::cIDStrLen> idStr;

    // create opaque part of url
    addParam("token", token.CStr(), true, opaque);

    (void)label; // label is not required, id should be enough to identify the object

    auto err = cryptoutils::EncodePKCS11ID(id, idStr);
    AOS_ERROR_CHECK_AND_THROW("PKCS11ID encoding problem", err);

    addParam("id", idStr.CStr(), true, opaque);

    addParam("pin-value", userPin.CStr(), false, query);

    // combine opaque & query parts of url
    StaticString<cURLLen> url;

    err = url.Format("pkcs11:%s?%s", opaque.c_str(), query.c_str());
    AOS_ERROR_CHECK_AND_THROW("RFC7512 URL format problem", err);

    return url.CStr();
}

static std::string CreatePKCS11URL(const String& keyURL)
{
    StaticString<cFilePathLen>      library;
    StaticString<pkcs11::cLabelLen> token;
    StaticString<pkcs11::cLabelLen> label;
    StaticString<pkcs11::cPINLen>   userPIN;
    uuid::UUID                      id;

    auto err = cryptoutils::ParsePKCS11URL(keyURL, library, token, label, id, userPIN);
    AOS_ERROR_CHECK_AND_THROW("URL parsing problem", err);

    return "engine:pkcs11:" + CreateRFC7512URL(token, label, id, userPIN);
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
        CreatePKCS11URL(certInfo.mKeyURL), ConvertCertificatesToPEM(chain, cryptoProvider)};

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
        CreatePKCS11URL(certInfo.mKeyURL), ConvertCertificatesToPEM(*certificates, cryptoProvider)};

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
    const String& rootCertPath, cryptoutils::CertLoader& certLoader, crypto::x509::ProviderItf& cryptoProvider)
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
    cryptoutils::CertLoader& certLoader, crypto::x509::ProviderItf& cryptoProvider)
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
