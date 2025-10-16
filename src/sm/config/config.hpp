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

#include <core/common/logprovider/config.hpp>
#include <core/common/monitoring/resourcemonitor.hpp>
#include <core/common/tools/error.hpp>
#include <core/sm/launcher/config.hpp>
#include <core/sm/layermanager/layermanager.hpp>
#include <core/sm/servicemanager/servicemanager.hpp>

#include <common/config/config.hpp>
#include <common/utils/time.hpp>
#include <sm/iamclient/iamclient.hpp>
#include <sm/smclient/config.hpp>

namespace aos::sm::config {

/***********************************************************************************************************************
 * Types
 **********************************************************************************************************************/

/*
 * Config instance.
 */
struct Config {
    iamclient::Config             mIAMClientConfig;
    sm::layermanager::Config      mLayerManagerConfig;
    sm::servicemanager::Config    mServiceManagerConfig;
    sm::launcher::Config          mLauncherConfig;
    smclient::Config              mSMClientConfig;
    std::string                   mCertStorage;
    std::string                   mIAMProtectedServerURL;
    std::string                   mWorkingDir;
    uint32_t                      mServicesPartLimit;
    uint32_t                      mLayersPartLimit;
    std::string                   mNodeConfigFile;
    monitoring::Config            mMonitoring;
    logprovider::Config           mLogging;
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
