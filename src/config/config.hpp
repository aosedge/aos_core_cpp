/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CONFIG_HPP_
#define CONFIG_HPP_

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <Poco/Dynamic/Var.h>

#include <aos/common/tools/error.hpp>
#include <utils/time.hpp>

/***********************************************************************************************************************
 * Types
 **********************************************************************************************************************/

/*
 * Identifier plugin parameters.
 */
struct Identifier {
    std::string        mPlugin;
    Poco::Dynamic::Var mParams;
};

/*
 * Remote IAM parameters.
 */
struct RemoteIAM {
    std::string                  mNodeID;
    std::string                  mURL;
    aos::common::utils::Duration mRequestTimeout;
};

/*
 * PKCS11 module parameters.
 */
struct PKCS11ModuleParams {
    std::string             mLibrary;
    std::optional<uint32_t> mSlotID;
    std::optional<int>      mSlotIndex;
    std::string             mTokenLabel;
    std::string             mUserPINPath;
    bool                    mModulePathInURL;
    uint32_t                mUID;
    uint32_t                mGID;
};

/*
 * VIS Identifier module parameters.
 */
struct VISIdentifierModuleParams {
    std::string mVISServer;
    std::string mCaCertFile;
    int         mWebSocketTimeout;
};

/*
 * Module configuration.
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
    std::string               mMigrationPath;
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

/*
 * Parses identifier plugin parameters.
 *
 * @param var Poco::Dynamic::Var instance.
 * @return Identifier instance.
 */
aos::RetWithError<PKCS11ModuleParams> ParsePKCS11ModuleParams(Poco::Dynamic::Var params);

/*
 * Parses VIS identifier plugin parameters.
 *
 * @param var Poco::Dynamic::Var instance.
 * @return VISIdentifierModuleParams instance.
 */
aos::RetWithError<VISIdentifierModuleParams> ParseVISIdentifierModuleParams(Poco::Dynamic::Var params);

#endif
