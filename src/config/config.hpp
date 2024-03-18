/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CONFIG_HPP_
#define CONFIG_HPP_

#include <string>
#include <utility>
#include <vector>

#include <Poco/Dynamic/Var.h>

#include <aos/common/tools/error.hpp>

#include "utils/time.hpp"

/***********************************************************************************************************************
 * Types
 **********************************************************************************************************************/

/*
 * Identifier identifier plugin parameters.
 */
struct Identifier {
    std::string        mPlugin;
    Poco::Dynamic::Var mParams;
};

/*
 * RemoteIAM remote IAM parameters.
 */
struct RemoteIAM {
    std::string         mNodeID;
    std::string         mURL;
    UtilsTime::Duration mRequestTimeout;
};

/*
 * ModuleConfig module configuration.
 */
struct ModuleConfig {
    std::string              mID;
    std::string              mPlugin;
    std::string              mAlgorithm;
    int                      mMaxItems;
    std::vector<std::string> mExtendedKeyUsage;
    std::vector<std::string> mAlternativeNames;
    bool                     mDisabled;
    bool                     mSkipValidation;
    Poco::Dynamic::Var       mParams;
};

/*
 * Config instance.
 */
struct Config {
    std::string               mIAMPublicServerURL;
    std::string               mIAMProtectedServerURL;
    std::string               mNodeID;
    std::string               mNodeType;
    std::string               mCACert;
    std::string               mCertStorage;
    std::string               mWorkingDir;
    std::vector<ModuleConfig> mCertModules;
    std::vector<std::string>  mFinishProvisioningCmdArgs;
    std::vector<std::string>  mDiskEncryptionCmdArgs;
    bool                      mEnablePermissionsHandler;
    Identifier                mIdentifier;
    std::vector<RemoteIAM>    mRemoteIAMs;
};

/*******************************************************************************
 * Functions
 ******************************************************************************/

/*
 * Parses config from file.
 *
 * @param filename config file name.
 * @return config instance.
 */
aos::RetWithError<Config> ParseConfig(const std::string& filename);

#endif
