/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_CONFIG_CONFIG_HPP_
#define AOS_COMMON_CONFIG_CONFIG_HPP_

#include <string>
#include <vector>

#include <aos/common/monitoring/resourcemonitor.hpp>
#include <aos/common/tools/error.hpp>

#include <common/utils/json.hpp>

namespace aos::common::config {

/***********************************************************************************************************************
 * Types
 **********************************************************************************************************************/

/*
 * Journal alerts configuration.
 */
struct JournalAlerts {
    std::vector<std::string> mFilter;
    int                      mServiceAlertPriority;
    int                      mSystemAlertPriority;
};

/*
 * Migration configuration.
 */
struct Migration {
    std::string mMigrationPath;
    std::string mMergedMigrationPath;
};

/***********************************************************************************************************************
 * Functions
 **********************************************************************************************************************/

/*
 * Parses monitoring configuration.
 *
 * @param object JSON object.
 * @param[out] config monitoring configuration.
 * @return Error.
 */
Error ParseMonitoringConfig(const common::utils::CaseInsensitiveObjectWrapper& object, monitoring::Config& config);

/*
 * Parses migration configuration.
 *
 * @param object JSON object.
 * @param defaultMigrationPath default migration path.
 * @param defaultMergedMigrationPath default merged migration path.
 * @param[out] config migration configuration.
 * @return Error.
 */
Error ParseMigrationConfig(const common::utils::CaseInsensitiveObjectWrapper& object,
    const std::string& defaultMigrationPath, const std::string& defaultMergedMigrationPath, Migration& config);

/*
 * Parses journal alerts configuration.
 *
 * @param object JSON object.
 * @param[out] config journal alerts configuration.
 * @return Error.
 */
Error ParseJournalAlertsConfig(const common::utils::CaseInsensitiveObjectWrapper& object, JournalAlerts& config);

} // namespace aos::common::config

#endif // AOS_COMMON_CONFIG_CONFIG_HPP_
