/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_UTILS_ITF_SYSTEMDCONN_HPP_
#define AOS_SM_UTILS_ITF_SYSTEMDCONN_HPP_

#include <string>
#include <vector>

#include <core/common/tools/error.hpp>
#include <core/common/tools/optional.hpp>
#include <core/common/tools/time.hpp>

namespace aos::sm::utils {

/**
 * Unit state.
 */
class UnitStateType {
public:
    enum class Enum {
        eActive,
        eInactive,
        eFailed,
        eActivating,
        eDeactivating,
        eMaintenance,
        eReloading,
        eRefreshing,
    };

    static const Array<const char* const> GetStrings()
    {
        static const char* const sNames[] = {
            "active",
            "inactive",
            "failed",
            "activating",
            "deactivating",
            "maintenance",
            "reloading",
            "refreshing",
        };

        return Array<const char* const>(sNames, ArraySize(sNames));
    };
};

using UnitStateEnum = UnitStateType::Enum;
using UnitState     = EnumStringer<UnitStateType>;

/**
 * Unit status.
 */
struct UnitStatus {
    std::string       mName;
    UnitState         mActiveState;
    Optional<int32_t> mExitCode;
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
    virtual Error StartUnit(const std::string& name, const std::string& mode, const Duration& timeout) = 0;

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

} // namespace aos::sm::utils

#endif
