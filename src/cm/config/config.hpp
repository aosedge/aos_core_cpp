/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_CONFIG_CONFIG_HPP_
#define AOS_CM_CONFIG_CONFIG_HPP_

#include <string>
#include <vector>

#include <core/cm/alerts/config.hpp>
#include <core/cm/imagemanager/config.hpp>
#include <core/cm/monitoring/config.hpp>
#include <core/cm/nodeinfoprovider/config.hpp>
#include <core/common/monitoring/config.hpp>
#include <core/common/tools/error.hpp>

#include <common/config/config.hpp>
#include <common/utils/time.hpp>

namespace aos::cm::config {

/***********************************************************************************************************************
 * Types
 **********************************************************************************************************************/

/*
 * Crypt configuration.
 */
struct Crypt {
    std::string mCACert;
    std::string mTpmDevice;
    std::string mPkcs11Library;
};

/*
 * Monitoring configuration.
 */
struct Monitoring : public aos::monitoring::Config, public aos::cm::monitoring::Config { };

/*
 * Downloader configuration.
 */
struct Downloader {
    std::string mDownloadDir;
    int         mMaxConcurrentDownloads;
    Duration    mRetryDelay;
    Duration    mMaxRetryDelay;
    int         mDownloadPartLimit;
};

/*
 * Config structure.
 */
struct Config {
    Crypt                     mCrypt;
    Monitoring                mMonitoring;
    nodeinfoprovider::Config  mNodeInfoProvider;
    alerts::Config            mAlerts;
    imagemanager::Config      mImageManager;
    common::config::Migration mMigration;
    Downloader                mDownloader;
    std::string               mDNSStoragePath;
    std::string               mDNSIP;
    std::string               mCertStorage;
    std::string               mServiceDiscoveryURL;
    std::string               mIAMProtectedServerURL;
    std::string               mIAMPublicServerURL;
    std::string               mCMServerURL;
    std::string               mStorageDir;
    std::string               mStateDir;
    std::string               mWorkingDir;
    std::string               mImageStoreDir;
    std::string               mComponentsDir;
    std::string               mUnitConfigFile;
    Duration                  mServiceTTL;
    Duration                  mLayerTTL;
    Duration                  mUnitStatusSendTimeout;
    Duration                  mCloudResponseWaitTimeout;
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

} // namespace aos::cm::config

#endif // AOS_CM_CONFIG_CONFIG_HPP_
