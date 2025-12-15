/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "iamclient.hpp"

#include <core/common/tools/log.hpp>

namespace aos::cm::iamclient {

IAMClient::~IAMClient()
{
    PublicCertService::UnsubscribeListener(*this);
}

Error IAMClient::Init(const std::string& iamProtectedServerURL, const std::string& iamPublicServerURL,
    const std::string& certStorage, aos::common::iamclient::TLSCredentialsItf& tlsCredentials, const String& certType,
    bool insecureConnection)
{
    LOG_INF() << "Initializing IAM client";

    auto err = CertificateService::Init(iamProtectedServerURL, certStorage, tlsCredentials, insecureConnection);
    if (!err.IsNone()) {
        return err;
    }

    err = NodesService::Init(iamProtectedServerURL, certStorage, tlsCredentials, insecureConnection);
    if (!err.IsNone()) {
        return err;
    }

    err = ProvisioningService::Init(iamProtectedServerURL, certStorage, tlsCredentials, insecureConnection);
    if (!err.IsNone()) {
        return err;
    }

    err = PublicCertService::Init(iamPublicServerURL, tlsCredentials, insecureConnection);
    if (!err.IsNone()) {
        return err;
    }

    err = PublicNodesService::Init(iamPublicServerURL, tlsCredentials, insecureConnection);
    if (!err.IsNone()) {
        return err;
    }

    err = PublicCertService::SubscribeListener(certType, *this);
    if (!err.IsNone()) {
        return err;
    }

    err = PublicCurrentNodeService::Init(iamPublicServerURL, tlsCredentials, insecureConnection);
    if (!err.IsNone()) {
        return err;
    }

    err = PublicIdentityService::Init(iamPublicServerURL, tlsCredentials, insecureConnection);
    if (!err.IsNone()) {
        return err;
    }

    LOG_INF() << "IAM client initialized successfully";

    return ErrorEnum::eNone;
}

void IAMClient::OnCertChanged([[maybe_unused]] const CertInfo& info)
{
    LOG_INF() << "Certificate changed, reconnect all services";

    auto err = CertificateService::Reconnect();
    if (!err.IsNone()) {
        LOG_ERR() << "Failed to reconnect certificate service" << Log::Field(err);
    }

    err = NodesService::Reconnect();
    if (!err.IsNone()) {
        LOG_ERR() << "Failed to reconnect nodes service" << Log::Field(err);
    }

    err = ProvisioningService::Reconnect();
    if (!err.IsNone()) {
        LOG_ERR() << "Failed to reconnect provisioning service" << Log::Field(err);
    }

    err = PublicCertService::Reconnect();
    if (!err.IsNone()) {
        LOG_ERR() << "Failed to reconnect public cert service" << Log::Field(err);
    }

    err = PublicNodesService::Reconnect();
    if (!err.IsNone()) {
        LOG_ERR() << "Failed to reconnect public nodes service" << Log::Field(err);
    }

    err = PublicCurrentNodeService::Reconnect();
    if (!err.IsNone()) {
        LOG_ERR() << "Failed to reconnect public current node service" << Log::Field(err);
    }

    err = PublicIdentityService::Reconnect();
    if (!err.IsNone()) {
        LOG_ERR() << "Failed to reconnect public identity service" << Log::Field(err);
    }
}

} // namespace aos::cm::iamclient
