/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_UTILS_GRPCCLIENTCERTLISTENER_HPP_
#define AOS_COMMON_UTILS_GRPCCLIENTCERTLISTENER_HPP_

#include <string>

#include <core/common/iamclient/itf/certprovider.hpp>
#include <core/common/tools/timer.hpp>

namespace aos::common::utils {

/**
 * Certificate update handler for grpc clients.
 */
class GRPCClientCertListener : public aos::iamclient::CertListenerItf {
public:
    /**
     * Constructor.
     *
     * @param clientName gRPC client name for logs.
     */
    explicit GRPCClientCertListener(const std::string& clientName);

    /**
     * Handles certificate updates by scheduling reconnect.
     *
     * @param info certificate info.
     */
    void OnCertChanged(const CertInfo& info) override;

    /**
     * Reconnects client.
     *
     * @returns Error.
     */
    virtual Error ReconnectClient() = 0;

protected:
    /**
     * Stops reconnect timer.
     */
    void StopReconnectTimer();

private:
    static constexpr Duration cReconnectRetryTimeout = Time::cSeconds * 10;

    void ScheduleReconnect();
    void OnReconnectTimer();

    const std::string mClientName;
    Timer             mReconnectTimer {};
};

} // namespace aos::common::utils

#endif
