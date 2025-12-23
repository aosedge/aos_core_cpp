/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_CONFIG_CONFIG_HPP_
#define AOS_SM_CONFIG_CONFIG_HPP_

#include <string>
#include <vector>

#include <Poco/Dynamic/Var.h>

#include <core/common/logging/config.hpp>
#include <core/common/monitoring/config.hpp>
#include <core/common/tools/error.hpp>

#include <common/config/config.hpp>
#include <common/iamclient/config.hpp>
#include <common/utils/time.hpp>
#include <sm/launcher/config.hpp>
#include <sm/smclient/config.hpp>

namespace aos::sm::config {

/***********************************************************************************************************************
 * Types
 **********************************************************************************************************************/

/*
 * Config instance.
 */
struct Config {
    common::iamclient::Config     mIAMClientConfig;
    smclient::Config              mSMClientConfig;
    std::string                   mCertStorage;
    std::string                   mIAMProtectedServerURL;
    std::string                   mWorkingDir;
    std::string                   mNodeConfigFile;
    monitoring::Config            mMonitoring;
    launcher::Config              mLauncher;
    logging::Config               mLogging;
    common::config::JournalAlerts mJournalAlerts;
    common::config::Migration     mMigration;
};

/*******************************************************************************
 * Functions
 ******************************************************************************/

/*
 * Parses config from file.
 *
 * @param filename config file name.
 * @param[out] config config instance.
 * @return Error.
 */
Error ParseConfig(const std::string& filename, Config& config);

} // namespace aos::sm::config

#endif
