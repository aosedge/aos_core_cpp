/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <unordered_map>

#include <common/utils/retry.hpp>
#include <core/common/tools/logger.hpp>

#include "systemdupdatechecker.hpp"

namespace aos::sm::launcher::utils {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error SystemdUpdateChecker::Init(const std::vector<std::string>& units, aos::sm::utils::SystemdConnItf& systemdConn)
{
    LOG_DBG() << "Initialize systemd update checker";

    std::transform(units.begin(), units.end(), std::inserter(mUnits, mUnits.end()),
        [](const auto& unit) { return std::make_pair(unit, aos::sm::utils::UnitStateEnum::eInactive); });

    mSystemdConn = &systemdConn;

    return ErrorEnum::eNone;
}

Error SystemdUpdateChecker::Check()
{
    LOG_DBG() << "Check for updates via systemd";

    auto retryFunc = [this]() -> Error {
        if (auto err = UpdateUnitsStatus(); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (AllUnitsActive() || AnyUnitFailed()) {
            return ErrorEnum::eNone;
        }

        return ErrorEnum::eRuntime;
    };

    if (auto err = common::utils::Retry(
            [&retryFunc]() { return retryFunc(); }, {}, cMaxRetryAttempts, cStartRetryDelay, cMaxRetryDelay);
        !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return AllUnitsActive() ? ErrorEnum::eNone : AOS_ERROR_WRAP(ErrorEnum::eFailed);
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

bool SystemdUpdateChecker::AllUnitsActive() const
{
    return std::all_of(mUnits.begin(), mUnits.end(),
        [](const auto& unitStatus) { return unitStatus.second == aos::sm::utils::UnitStateEnum::eActive; });
}

bool SystemdUpdateChecker::AnyUnitFailed() const
{
    return std::any_of(mUnits.begin(), mUnits.end(),
        [](const auto& unitStatus) { return unitStatus.second == aos::sm::utils::UnitStateEnum::eFailed; });
}

Error SystemdUpdateChecker::UpdateUnitsStatus()
{
    for (auto& [unit, state] : mUnits) {
        auto ret = mSystemdConn->GetUnitStatus(unit);
        if (!ret.mError.IsNone()) {
            LOG_ERR() << "Can't get unit status" << Log::Field("unit", unit.c_str()) << Log::Field(ret.mError);

            return AOS_ERROR_WRAP(ret.mError);
        }

        state = ret.mValue.mActiveState;
    }

    return ErrorEnum::eNone;
}

} // namespace aos::sm::launcher::utils
