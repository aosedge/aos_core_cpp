/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_UTILS_SYSTEMDREBOOTER_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_UTILS_SYSTEMDREBOOTER_HPP_

#include <core/common/tools/time.hpp>
#include <core/sm/launcher/itf/rebooter.hpp>
#include <sm/utils/itf/systemdconn.hpp>

namespace aos::sm::launcher::utils {

/**
 * Rebooter interface.
 */
class SystemdRebooter : public RebooterItf {
public:
    /**
     * Initializes the rebooter.
     *
     * @param systemdConn systemd connection.
     * @return Error.
     */
    Error Init(aos::sm::utils::SystemdConnItf& systemdConn);

    /**
     * Reboots the system.
     *
     * @return Error.
     */
    Error Reboot() override;

private:
    static constexpr auto cRebootTarget = "reboot.target";
    static constexpr auto cReplaceMode  = "replace";
    static constexpr auto cTimeout      = Time::cMinutes;

    aos::sm::utils::SystemdConnItf* mSystemdConn {};
};

} // namespace aos::sm::launcher::utils

#endif
