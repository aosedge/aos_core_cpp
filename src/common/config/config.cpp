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

void ParseMonitoringConfig(const common::utils::CaseInsensitiveObjectWrapper& object, monitoring::Config& config)
{
    const auto pollPeriod    = object.GetValue<std::string>("pollPeriod", cDefaultMonitoringPollPeriod);
    const auto averageWindow = object.GetValue<std::string>("averageWindow", cDefaultMonitoringAverageWindow);

    Error err = ErrorEnum::eNone;

    Tie(config.mPollPeriod, err) = common::utils::ParseDuration(pollPeriod);
    AOS_ERROR_CHECK_AND_THROW(err, "error parsing pollPeriod tag");

    Tie(config.mAverageWindow, err) = common::utils::ParseDuration(averageWindow);
    AOS_ERROR_CHECK_AND_THROW(err, "error parsing averageWindow tag");
}

void ParseMigrationConfig(const common::utils::CaseInsensitiveObjectWrapper& object,
    const std::string& defaultMigrationPath, const std::string& defaultMergedMigrationPath, Migration& config)
{
    config.mMigrationPath = object.GetOptionalValue<std::string>("migrationPath").value_or(defaultMigrationPath);
    config.mMergedMigrationPath
        = object.GetOptionalValue<std::string>("mergedMigrationPath").value_or(defaultMergedMigrationPath);
}

void ParseJournalAlertsConfig(const common::utils::CaseInsensitiveObjectWrapper& object, JournalAlerts& config)
{
    config.mFilter = common::utils::GetArrayValue<std::string>(object, "filter");

    config.mServiceAlertPriority
        = object.GetOptionalValue<int>("serviceAlertPriority").value_or(cDefaultServiceAlertPriority);
    if (config.mServiceAlertPriority > cMaxAlertPriorityLevel
        || config.mServiceAlertPriority < cMinAlertPriorityLevel) {
        config.mServiceAlertPriority = cDefaultServiceAlertPriority;
    }

    config.mSystemAlertPriority
        = object.GetOptionalValue<int>("systemAlertPriority").value_or(cDefaultSystemAlertPriority);
    if (config.mSystemAlertPriority > cMaxAlertPriorityLevel || config.mSystemAlertPriority < cMinAlertPriorityLevel) {
        config.mSystemAlertPriority = cDefaultSystemAlertPriority;
    }
}

} // namespace aos::common::config
