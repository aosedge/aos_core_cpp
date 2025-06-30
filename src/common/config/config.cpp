/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>

#include <common/utils/exception.hpp>
#include <common/utils/time.hpp>

#include "config.hpp"

namespace aos::common::config {

/***********************************************************************************************************************
 * Constants
 **********************************************************************************************************************/

constexpr auto cDefaultMonitoringPollPeriod    = "35s";
constexpr auto cDefaultMonitoringAverageWindow = "35s";
constexpr auto cDefaultServiceAlertPriority    = 4;
constexpr auto cDefaultSystemAlertPriority     = 3;
constexpr auto cMaxAlertPriorityLevel          = 7;
constexpr auto cMinAlertPriorityLevel          = 0;

/***********************************************************************************************************************
 * Public functions
 **********************************************************************************************************************/

Error ParseMonitoringConfig(const common::utils::CaseInsensitiveObjectWrapper& object, monitoring::Config& config)
{
    try {
        const auto pollPeriod    = object.GetValue<std::string>("pollPeriod", cDefaultMonitoringPollPeriod);
        const auto averageWindow = object.GetValue<std::string>("averageWindow", cDefaultMonitoringAverageWindow);

        Error err = ErrorEnum::eNone;

        Tie(config.mPollPeriod, err) = common::utils::ParseDuration(pollPeriod);
        if (err != ErrorEnum::eNone) {
            return AOS_ERROR_WRAP(err);
        }

        Tie(config.mAverageWindow, err) = common::utils::ParseDuration(averageWindow);

        return AOS_ERROR_WRAP(err);
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }
}

Error ParseMigrationConfig(const common::utils::CaseInsensitiveObjectWrapper& object,
    const std::string& defaultMigrationPath, const std::string& defaultMergedMigrationPath, Migration& config)
{
    try {
        config.mMigrationPath = object.GetOptionalValue<std::string>("migrationPath").value_or(defaultMigrationPath);
        config.mMergedMigrationPath
            = object.GetOptionalValue<std::string>("mergedMigrationPath").value_or(defaultMergedMigrationPath);
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error ParseJournalAlertsConfig(const common::utils::CaseInsensitiveObjectWrapper& object, JournalAlerts& config)
{
    try {
        config.mFilter = common::utils::GetArrayValue<std::string>(object, "filter");

        config.mServiceAlertPriority
            = object.GetOptionalValue<int>("serviceAlertPriority").value_or(cDefaultServiceAlertPriority);
        if (config.mServiceAlertPriority > cMaxAlertPriorityLevel
            || config.mServiceAlertPriority < cMinAlertPriorityLevel) {
            config.mServiceAlertPriority = cDefaultServiceAlertPriority;
        }

        config.mSystemAlertPriority
            = object.GetOptionalValue<int>("systemAlertPriority").value_or(cDefaultSystemAlertPriority);
        if (config.mSystemAlertPriority > cMaxAlertPriorityLevel
            || config.mSystemAlertPriority < cMinAlertPriorityLevel) {
            config.mSystemAlertPriority = cDefaultSystemAlertPriority;
        }
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::common::config
