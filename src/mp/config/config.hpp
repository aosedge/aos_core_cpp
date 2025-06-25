/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_MP_CONFIG_CONFIG_HPP_
#define AOS_MP_CONFIG_CONFIG_HPP_

#include <string>

#include <aos/common/logprovider/config.hpp>
#include <aos/common/tools/error.hpp>

#include <common/utils/time.hpp>

namespace aos::mp::config {

/***********************************************************************************************************************
 * Types
 **********************************************************************************************************************/

/*
 * Downloader configuration.
 */
struct Download {
    std::string mDownloadDir;
    int         mMaxConcurrentDownloads;
    Duration    mRetryDelay;
    Duration    mMaxRetryDelay;
};

/*
 * VChan configuration.
 */
struct VChanConfig {
    int         mDomain;
    std::string mXSRXPath;
    std::string mXSTXPath;
    std::string mIAMCertStorage;
    std::string mSMCertStorage;
};

/*
 * IAM configuration.
 */
struct IAMConfig {
    std::string mIAMPublicServerURL;
    std::string mIAMProtectedServerURL;
    std::string mCertStorage;
    int         mOpenPort;
    int         mSecurePort;
};

/*
 * CM configuration.
 */
struct CMConfig {
    std::string mCMServerURL;
    int         mOpenPort;
    int         mSecurePort;
};

/*
 * Configuration.
 */
struct Config {
    std::string              mWorkingDir;
    VChanConfig              mVChan;
    CMConfig                 mCMConfig;
    std::string              mCertStorage;
    std::string              mCACert;
    std::string              mImageStoreDir;
    Download                 mDownload;
    IAMConfig                mIAMConfig;
    aos::logprovider::Config mLogProviderConfig;
};

/***********************************************************************************************************************
 * Functions
 **********************************************************************************************************************/

/*
 * Parses configuration from the file.
 *
 * @param filename Configuration file name.
 * @return RetWithError<Config> Configuration.
 */
RetWithError<Config> ParseConfig(const std::string& filename);

} // namespace aos::mp::config

#endif
