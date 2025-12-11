/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_UTILS_SYSTEMDUPDATECHECKER_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_UTILS_SYSTEMDUPDATECHECKER_HPP_

#include <unordered_map>

#include <core/common/tools/time.hpp>
#include <core/sm/launcher/itf/updatechecker.hpp>
#include <sm/utils/itf/systemdconn.hpp>

namespace aos::sm::launcher::utils {

/**
 * Systemd update checker.
 */
class SystemdUpdateChecker : public UpdateCheckerItf {
public:
    /**
     * Initializes the update checker.
     *
     * @param units list of units to check.
     * @param systemdConn systemd connection.
     * @return Error.
     */
    Error Init(const std::vector<std::string>& units, aos::sm::utils::SystemdConnItf& systemdConn);

    /**
     * Checks if update succeeded.
     *
     * @return Error.
     */
    Error Check() override;

private:
    static constexpr auto cStartRetryDelay  = 10 * Time::cSeconds;
    static constexpr auto cMaxRetryDelay    = 1 * Time::cMinutes;
    static constexpr auto cMaxRetryAttempts = 5;

    bool  AllUnitsActive() const;
    bool  AnyUnitFailed() const;
    Error UpdateUnitsStatus();

    std::unordered_map<std::string, aos::sm::utils::UnitStateEnum> mUnits;
    aos::sm::utils::SystemdConnItf*                                mSystemdConn {};
};

} // namespace aos::sm::launcher::utils

#endif
