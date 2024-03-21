/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "grpchelper.hpp"

#include <utils/exception.hpp>

using namespace aos;

/***********************************************************************************************************************
 * Statics
 **********************************************************************************************************************/

static StaticString<uuid::cUUIDLen * 3> PercentEncodeID(const uuid::UUID& id)
{
    StaticString<uuid::cUUIDLen * 3> result;

    for (const auto& val : id) {
        aos::Pair<char, char> chunk = String::ByteToHex(val);

        result.PushBack('%');
        result.PushBack(chunk.mFirst);
        result.PushBack(chunk.mSecond);
    }

    *result.end() = 0;

    return result;
}

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

    std::string opaque, query;

    // create opaque part of url
    addParam("token", token.CStr(), true, opaque);

    (void)label; // label is not required, id should be enough to identify the object

    if (!id.IsEmpty()) {
        auto uuid = PercentEncodeID(id);
        addParam("id", uuid.CStr(), true, opaque);
    }

    addParam("pin-value", userPin.CStr(), false, query);

    // combine opaque & query parts of url
    StaticString<cURLLen> url;

    auto err = url.Format("pkcs11:%s?%s", opaque.c_str(), query.c_str());
    AOS_ERROR_CHECK_AND_THROW("RFC7512 URL format problem", err);

    return url.CStr();
}

static std::string CreatePKCS11URL(const String& keyURL)
{
    StaticString<cFilePathLen>       library;
    StaticString<pkcs11::cLabelLen>  token;
    StaticString<pkcs11::cLabelLen>  label;
    StaticString<pkcs11::cPINLength> userPIN;
    uuid::UUID                       id;

    auto err = cryptoutils::ParsePKCS11URL(keyURL, library, token, label, id, userPIN);
    AOS_ERROR_CHECK_AND_THROW("URL parsing problem", err);

    return "engine:pkcs11:" + CreateRFC7512URL(token, label, id, userPIN);
}

static std::string ConvertCertificateToPEM(
    const crypto::x509::Certificate& certificate, crypto::x509::ProviderItf& cryptoProvider)
{
    std::string result;

    result.resize(crypto::cCertPEMLen);

    String view = result.c_str();

    view.Resize(crypto::cCertPEMLen);

    auto err = cryptoProvider.X509CertToPEM(certificate, view);
    AOS_ERROR_CHECK_AND_THROW("Certificate convertion problem", err);

    result.resize(view.Size());

    return result;
}

static std::shared_ptr<grpc::experimental::CertificateProviderInterface> GetMTLSCertificates(
    const iam::certhandler::CertInfo& certInfo, cryptoutils::CertLoaderItf& certLoader,
    crypto::x509::ProviderItf& cryptoProvider)
{
    auto [certificates, err] = certLoader.LoadCertsChainByURL(certInfo.mCertURL);

    AOS_ERROR_CHECK_AND_THROW("Load certificate by URL failed", err);

    if (certificates->Size() != 2) {
        throw std::runtime_error("Not expected number of certificates in the chain");
    }

    auto rootCert = ConvertCertificateToPEM((*certificates)[1], cryptoProvider);

    auto keyCertPair = grpc::experimental::IdentityKeyCertPair {
        CreatePKCS11URL(certInfo.mKeyURL), ConvertCertificateToPEM((*certificates)[0], cryptoProvider)};

    std::vector<grpc::experimental::IdentityKeyCertPair> keyCertPairs = {keyCertPair};

    return std::make_shared<grpc::experimental::StaticDataCertificateProvider>(rootCert, keyCertPairs);
}

static std::shared_ptr<grpc::experimental::CertificateProviderInterface> GetTLSCertificates(
    const iam::certhandler::CertInfo& certInfo, cryptoutils::CertLoaderItf& certLoader,
    crypto::x509::ProviderItf& cryptoProvider)
{
    auto [certificates, err] = certLoader.LoadCertsChainByURL(certInfo.mCertURL);

    AOS_ERROR_CHECK_AND_THROW("Load certificate by URL failed", err);

    if (certificates->Size() < 1) {
        throw std::runtime_error("Not expected number of certificates in the chain");
    }

    auto keyCertPair = grpc::experimental::IdentityKeyCertPair {
        CreatePKCS11URL(certInfo.mKeyURL), ConvertCertificateToPEM((*certificates)[0], cryptoProvider)};

    std::vector<grpc::experimental::IdentityKeyCertPair> keyCertPairs = {keyCertPair};

    return std::make_shared<grpc::experimental::StaticDataCertificateProvider>("", keyCertPairs);
}

/***********************************************************************************************************************
 * Public interface
 **********************************************************************************************************************/

std::shared_ptr<grpc::ServerCredentials> GetMTLSCredentials(const iam::certhandler::CertInfo& certInfo,
    cryptoutils::CertLoader& certLoader, crypto::x509::ProviderItf& cryptoProvider)
{
    auto certificates = GetMTLSCertificates(certInfo, certLoader, cryptoProvider);

    grpc::experimental::TlsServerCredentialsOptions options {certificates};

    options.set_cert_request_type(GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
    options.set_check_call_host(false);
    options.watch_root_certs();
    options.watch_identity_key_cert_pairs();
    options.set_root_cert_name("root");
    options.set_identity_cert_name("identity");

    return grpc::experimental::TlsServerCredentials(options);
}

std::shared_ptr<grpc::ServerCredentials> GetTLSCredentials(const iam::certhandler::CertInfo& certInfo,
    cryptoutils::CertLoader& certLoader, crypto::x509::ProviderItf& cryptoProvider)
{
    auto certificates = GetTLSCertificates(certInfo, certLoader, cryptoProvider);

    grpc::experimental::TlsServerCredentialsOptions options {certificates};

    options.set_cert_request_type(GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE);
    options.set_check_call_host(false);
    options.watch_identity_key_cert_pairs();
    options.set_identity_cert_name("identity");

    return grpc::experimental::TlsServerCredentials(options);
}

std::shared_ptr<grpc::ChannelCredentials> GetTlsChannelCredentials(const aos::iam::certhandler::CertInfo& certInfo,
    aos::cryptoutils::CertLoaderItf& certLoader, aos::crypto::x509::ProviderItf& cryptoProvider)
{
    auto certificates = GetTLSCertificates(certInfo, certLoader, cryptoProvider);

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
