/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_IAM_CONFIG_CONFIG_HPP_
#define AOS_IAM_CONFIG_CONFIG_HPP_

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <Poco/Dynamic/Var.h>

#include <core/common/tools/error.hpp>
#include <core/iam/identhandler/identmodules/fileidentifier/config.hpp>

#include <common/utils/time.hpp>

namespace aos::iam::config {

/***********************************************************************************************************************
 * Types
 **********************************************************************************************************************/

/*
 * Identifier plugin parameters.
 */
struct IdentifierConfig {
    std::string        mPlugin;
    Poco::Dynamic::Var mParams;
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
    Duration    mWebSocketTimeout;
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
    bool                     mIsSelfSigned;
    Poco::Dynamic::Var       mParams;
};

/*
 * Partition information configuration.
 */
struct PartitionInfoConfig {
    std::string              mName;
    std::vector<std::string> mTypes;
    std::string              mPath;
};

/*
 * Node information configuration.
 */
struct NodeInfoConfig {
    std::string                                  mCPUInfoPath;
    std::string                                  mMemInfoPath;
    std::string                                  mProvisioningStatePath;
    std::string                                  mNodeIDPath;
    std::string                                  mNodeName;
    std::string                                  mNodeType;
    uint64_t                                     mMaxDMIPS;
    std::unordered_map<std::string, std::string> mAttrs;
    std::vector<PartitionInfoConfig>             mPartitions;
    std::optional<std::string>                   mArchitecture;
    std::optional<std::string>                   mArchitectureVariant;
    std::optional<std::string>                   mOS;
    std::optional<std::string>                   mOSVersion;
};

/**
 * Database configuration.
 */
struct DatabaseConfig {
    std::string                        mWorkingDir;
    std::string                        mMigrationPath;
    std::string                        mMergedMigrationPath;
    std::map<std::string, std::string> mPathToPin;
};

/**
 * Common config params for IAM client/server.
 */
struct IAMConfig {
    std::string              mCACert;
    std::string              mCertStorage;
    std::vector<std::string> mStartProvisioningCmdArgs;
    std::vector<std::string> mDiskEncryptionCmdArgs;
    std::vector<std::string> mFinishProvisioningCmdArgs;
    std::vector<std::string> mDeprovisionCmdArgs;
};

/**
 * Configuration for IAM client.
 */
struct IAMClientConfig : IAMConfig {
    std::string mMainIAMPublicServerURL;
    std::string mMainIAMProtectedServerURL;
    Duration    mNodeReconnectInterval;
};

/**
 * Configuration for IAM client.
 */
struct IAMServerConfig : IAMConfig {
    std::string mIAMPublicServerURL;
    std::string mIAMProtectedServerURL;
};

/*
 * Config instance.
 */
struct Config {
    NodeInfoConfig            mNodeInfo;
    IAMClientConfig           mIAMClient;
    IAMServerConfig           mIAMServer;
    DatabaseConfig            mDatabase;
    IdentifierConfig          mIdentifier;
    std::vector<ModuleConfig> mCertModules;
    bool                      mEnablePermissionsHandler;
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
RetWithError<Config> ParseConfig(const std::string& filename);

/*
 * Parses identifier plugin parameters.
 *
 * @param var Poco::Dynamic::Var instance.
 * @return Identifier instance.
 */
RetWithError<PKCS11ModuleParams> ParsePKCS11ModuleParams(Poco::Dynamic::Var params);

/*
 * Parses VIS identifier plugin parameters.
 *
 * @param var Poco::Dynamic::Var instance.
 * @return VISIdentifierModuleParams instance.
 */
RetWithError<VISIdentifierModuleParams> ParseVISIdentifierModuleParams(Poco::Dynamic::Var params);

/*
 * Parses file identifier plugin parameters.
 *
 * @param var Poco::Dynamic::Var instance.
 * @param[out] config parsed config.
 * @return Error.
 */
Error ParseFileIdentifierModuleParams(Poco::Dynamic::Var params, iam::identhandler::FileIdentifierConfig& config);

} // namespace aos::iam::config

#endif
