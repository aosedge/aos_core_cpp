/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SYSTEMDCONN_HPP_
#define SYSTEMDCONN_HPP_

#include <mutex>
#include <string>
#include <systemd/sd-bus.h>
#include <vector>

#include <aos/common/tools/error.hpp>
#include <aos/common/tools/time.hpp>
#include <aos/sm/runner.hpp>

namespace aos::sm::runner {

/**
 * Unit status.
 */
struct UnitStatus {
    std::string      mName;
    InstanceRunState mActiveState;
};

/**
 * Systemd dbus connection interface.
 */
class SystemdConnItf {
public:
    /**
     * Destructor.
     */
    virtual ~SystemdConnItf() = default;

    /**
     * Returns a list of systemd units.
     *
     * @return RetWithError<std::vector<UnitStatus>>.
     */
    virtual RetWithError<std::vector<UnitStatus>> ListUnits() = 0;

    /**
     * Returns a status of systemd unit.
     *
     * @param name unit name.
     * @return RetWithError<UnitStatus>.
     */
    virtual RetWithError<UnitStatus> GetUnitStatus(const std::string& name) = 0;

    /**
     * Starts a unit.
     *
     * @param name unit name.
     * @param mode start mode.
     * @param timeout timeout.
     * @return Error.
     */
    virtual Error StartUnit(const std::string& name, const std::string& mode, const aos::Duration& timeout) = 0;

    /**
     * Stops a unit.
     *
     * @param name unit name.
     * @param mode start mode.
     * @return Error.
     */
    virtual Error StopUnit(const std::string& name, const std::string& mode, const Duration& timeout) = 0;

    /**
     * Resets the "failed" state of a specific unit.
     *
     * @param name unit name.
     * @return Error.
     */
    virtual Error ResetFailedUnit(const std::string& name) = 0;
};

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
    static constexpr auto cDestination = "org.freedesktop.systemd1";
    static constexpr auto cPath        = "/org/freedesktop/systemd1";
    static constexpr auto cInterface   = "org.freedesktop.systemd1.Manager";

    Error                  WaitForJobCompletion(const char* jobPath, const Duration& timeout);
    std::pair<bool, Error> HandleJobRemove(sd_bus_message* m, const char* jobPath);

    sd_bus*    mBus = nullptr;
    std::mutex mMutex;
};

} // namespace aos::sm::runner

#endif // SYSTEMDCONN_HPP_
