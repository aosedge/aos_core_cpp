/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <core/common/tools/log.hpp>

#include "iamclient.hpp"

namespace aos::sm::iamclient {

IAMClient::~IAMClient()
{
    PublicCertService::UnsubscribeListener(*this);
}

Error IAMClient::Init(const std::string& iamProtectedServerURL, const std::string& iamPublicServerURL,
    const std::string& certStorage, aos::common::iamclient::TLSCredentialsItf& tlsCredentials, const String& certType,
    bool insecureConnection)
{
    LOG_DBG() << "Init IAM client";

    auto err = PublicCertService::Init(iamPublicServerURL, tlsCredentials, insecureConnection);
    if (!err.IsNone()) {
        return err;
    }

    err = PublicCurrentNodeService::Init(iamPublicServerURL, tlsCredentials, insecureConnection);
    if (!err.IsNone()) {
        return err;
    }

    err = PublicCertService::SubscribeListener(certType, *this);
    if (!err.IsNone()) {
        return err;
    }

    err = PermissionsService::Init(iamProtectedServerURL, certStorage, tlsCredentials, insecureConnection);
    if (!err.IsNone()) {
        return err;
    }

    return ErrorEnum::eNone;
}

void IAMClient::OnCertChanged([[maybe_unused]] const CertInfo& info)
{
    LOG_INF() << "Certificate changed, reconnect all services";

    auto err = PublicCertService::Reconnect();
    if (!err.IsNone()) {
        LOG_ERR() << "Failed to reconnect public cert service" << Log::Field(err);
    }

    err = PublicCurrentNodeService::Reconnect();
    if (!err.IsNone()) {
        LOG_ERR() << "Failed to reconnect public current node service" << Log::Field(err);
    }

    err = PermissionsService::Reconnect();
    if (!err.IsNone()) {
        LOG_ERR() << "Failed to reconnect permissions service" << Log::Field(err);
    }
}

} // namespace aos::sm::iamclient
