/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <core/common/tools/logger.hpp>

#include "grpcclientcertlistener.hpp"

namespace aos::common::utils {

GRPCClientCertListener::GRPCClientCertListener(const std::string& clientName)
    : mClientName(clientName)
{
}

void GRPCClientCertListener::OnCertChanged([[maybe_unused]] const CertInfo& info)
{
    LOG_INF() << "Certificate changed, schedule reconnects" << Log::Field("client", mClientName.c_str());

    ScheduleReconnect();
}

void GRPCClientCertListener::StopReconnectTimer()
{
    if (auto err = mReconnectTimer.Stop(); !err.IsNone() && !err.Is(ErrorEnum::eWrongState)) {
        LOG_ERR() << "Failed to stop reconnect timer" << Log::Field("client", mClientName.c_str()) << Log::Field(err);
    }
}

void GRPCClientCertListener::ScheduleReconnect()
{
    StopReconnectTimer();

    if (auto err = mReconnectTimer.Start(
            cReconnectRetryTimeout, [this](void*) { OnReconnectTimer(); }, true);
        !err.IsNone()) {
        LOG_ERR() << "Failed to start reconnect timer" << Log::Field("client", mClientName.c_str()) << Log::Field(err);
    }
}

void GRPCClientCertListener::OnReconnectTimer()
{
    auto err = ReconnectClient();
    if (err.IsNone()) {
        LOG_INF() << "Successfully reconnected" << Log::Field("client", mClientName.c_str());

        return;
    }

    LOG_ERR() << "Reconnect failed, retrying" << Log::Field("client", mClientName.c_str()) << Log::Field(err);

    ScheduleReconnect();
}

} // namespace aos::common::utils
