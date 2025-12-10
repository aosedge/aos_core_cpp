/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_UTILS_SYSTEMDCONN_HPP_
#define AOS_SM_UTILS_SYSTEMDCONN_HPP_

#include <mutex>
#include <systemd/sd-bus.h>
#include <vector>

#include "itf/systemdconn.hpp"

namespace aos::sm::utils {

/**
 * Systemd dbus connection.
 */
class SystemdConn : public SystemdConnItf {
public:
    /**
     * Constructor.
     */
    SystemdConn();

    /**
     * Destructor.
     */
    ~SystemdConn();

    /**
     * Returns a list of systemd units.
     *
     * @return RetWithError<std::vector<UnitStatus>>.
     */
    RetWithError<std::vector<UnitStatus>> ListUnits() override;

    /**
     * Returns a status of systemd unit.
     *
     * @param name unit name.
     * @return RetWithError<UnitStatus>.
     */
    RetWithError<UnitStatus> GetUnitStatus(const std::string& name) override;

    /**
     * Starts a unit.
     *
     * @param name unit name.
     * @param mode start mode.
     * @param timeout timeout.
     * @return Error.
     */
    Error StartUnit(const std::string& name, const std::string& mode, const Duration& timeout) override;

    /**
     * Stops a unit.
     *
     * @param name unit name.
     * @param mode start mode.
     * @param timeout timeout.
     * @return Error.
     */
    Error StopUnit(const std::string& name, const std::string& mode, const Duration& timeout) override;

    /**
     * Resets the "failed" state of a specific unit.
     *
     * @param name unit name.
     * @return Error.
     */
    Error ResetFailedUnit(const std::string& name) override;

private:
    static constexpr auto cDestination   = "org.freedesktop.systemd1";
    static constexpr auto cPath          = "/org/freedesktop/systemd1";
    static constexpr auto cInterface     = "org.freedesktop.systemd1.Manager";
    static constexpr auto cNoSuchUnitErr = "org.freedesktop.systemd1.NoSuchUnit";

    Error                  WaitForJobCompletion(const char* jobPath, const Duration& timeout);
    std::pair<bool, Error> HandleJobRemove(sd_bus_message* m, const char* jobPath);
    Optional<int32_t>      GetExitCode(const char* serviceName);

    sd_bus*    mBus = nullptr;
    std::mutex mMutex;
};

} // namespace aos::sm::utils

#endif // SYSTEMDCONN_HPP_
