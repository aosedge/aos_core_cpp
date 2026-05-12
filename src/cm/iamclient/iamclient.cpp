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
    if (auto err = mReconnectTimer.Stop(); !err.IsNone() && !err.Is(ErrorEnum::eWrongState)) {
        LOG_ERR() << "Failed to stop reconnect timer" << Log::Field(err);
    }

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

    err = CertificateService::Init(iamProtectedServerURL, certStorage, tlsCredentials, insecureConnection);
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

    return ErrorEnum::eNone;
}

void IAMClient::OnCertChanged([[maybe_unused]] const CertInfo& info)
{
    LOG_INF() << "Certificate changed, reconnect IAM client";

    ScheduleReconnect();
}

Error IAMClient::ReconnectAllServices()
{
    auto err = PublicCertService::Reconnect();
    if (!err.IsNone()) {
        LOG_WRN() << "Failed to reconnect public cert service" << Log::Field(err);

        return err;
    }

    err = CertificateService::Reconnect();
    if (!err.IsNone()) {
        LOG_WRN() << "Failed to reconnect certificate service" << Log::Field(err);

        return err;
    }

    err = NodesService::Reconnect();
    if (!err.IsNone()) {
        LOG_WRN() << "Failed to reconnect nodes service" << Log::Field(err);

        return err;
    }

    err = ProvisioningService::Reconnect();
    if (!err.IsNone()) {
        LOG_WRN() << "Failed to reconnect provisioning service" << Log::Field(err);

        return err;
    }

    err = PublicNodesService::Reconnect();
    if (!err.IsNone()) {
        LOG_WRN() << "Failed to reconnect public nodes service" << Log::Field(err);

        return err;
    }

    err = PublicCurrentNodeService::Reconnect();
    if (!err.IsNone()) {
        LOG_WRN() << "Failed to reconnect public current node service" << Log::Field(err);

        return err;
    }

    err = PublicIdentityService::Reconnect();
    if (!err.IsNone()) {
        LOG_WRN() << "Failed to reconnect public identity service" << Log::Field(err);

        return err;
    }

    return ErrorEnum::eNone;
}

void IAMClient::ScheduleReconnect()
{
    if (auto err = mReconnectTimer.Stop(); !err.IsNone() && !err.Is(ErrorEnum::eWrongState)) {
        LOG_ERR() << "Failed to stop reconnect timer" << Log::Field(err);
    }

    if (auto err = mReconnectTimer.Start(
            cReconnectRetryTimeout, [this](void*) { OnReconnectTimer(); }, true);
        !err.IsNone()) {
        LOG_ERR() << "Failed to start reconnect timer" << Log::Field(err);
    }
}

void IAMClient::OnReconnectTimer()
{
    auto err = ReconnectAllServices();
    if (err.IsNone()) {
        LOG_INF() << "Successfully reconnected all IAM services";

        return;
    }

    LOG_ERR() << "IAM services reconnect failed, retrying" << Log::Field(err);

    ScheduleReconnect();
}

} // namespace aos::cm::iamclient
