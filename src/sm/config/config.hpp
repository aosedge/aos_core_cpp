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
#include <core/sm/imagemanager/config.hpp>

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
    std::string                   mCertStorage;
    std::string                   mIAMProtectedServerURL;
    std::string                   mNodeConfigFile;
    std::string                   mWorkingDir;
    common::config::JournalAlerts mJournalAlerts;
    common::config::Migration     mMigration;
    common::iamclient::Config     mIAMClientConfig;
    imagemanager::Config          mImageManager;
    launcher::Config              mLauncher;
    logging::Config               mLogging;
    monitoring::Config            mMonitoring;
    smclient::Config              mSMClientConfig;
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
