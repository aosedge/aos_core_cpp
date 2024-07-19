/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <fstream>
#include <iostream>
#include <unordered_map>

#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>

#include <utils/json.hpp>

#include "config.hpp"
#include "logger/logmodule.hpp"

/***********************************************************************************************************************
 * Constants
 **********************************************************************************************************************/

constexpr auto cDefaultCPUInfoPath            = "/proc/cpuinfo";
constexpr auto cDefaultMemInfoPath            = "/proc/meminfo";
constexpr auto cDefaultProvisioningStatusPath = "/var/aos/.provisionstate";
constexpr auto cDefaultNodeIDPath             = "/etc/machine-id";

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

static Identifier ParseIdentifier(const aos::common::utils::CaseInsensitiveObjectWrapper& object)
{
    return Identifier {object.GetValue<std::string>("plugin"), object.Get("params")};
}

static ModuleConfig ParseModuleConfig(const aos::common::utils::CaseInsensitiveObjectWrapper& object)
{
    return ModuleConfig {
        object.GetValue<std::string>("id"),
        object.GetValue<std::string>("plugin"),
        object.GetValue<std::string>("algorithm"),
        object.GetValue<int>("maxItems"),
        aos::common::utils::GetArrayValue<std::string>(
            object, "extendedKeyUsage", [](const Poco::Dynamic::Var& value) { return value.convert<std::string>(); }),
        aos::common::utils::GetArrayValue<std::string>(
            object, "alternativeNames", [](const Poco::Dynamic::Var& value) { return value.convert<std::string>(); }),
        object.GetValue<bool>("disabled"),
        object.GetValue<bool>("skipValidation"),
        object.GetValue<bool>("selfSigned"),
        object.Get("params"),
    };
}

static PartitionInfoConfig ParsePartitionInfoConfig(const aos::common::utils::CaseInsensitiveObjectWrapper& object)
{
    PartitionInfoConfig partitionInfoConfig {};

    partitionInfoConfig.mName = object.GetValue<std::string>("name");
    partitionInfoConfig.mPath = object.GetValue<std::string>("path");

    const auto& types = aos::common::utils::GetArrayValue<std::string>(
        object, "types", [](const Poco::Dynamic::Var& value) { return value.convert<std::string>(); });

    for (const auto& type : types) {
        partitionInfoConfig.mTypes.push_back(type);
    }

    return partitionInfoConfig;
}

static NodeInfoConfig ParseNodeInfoConfig(const aos::common::utils::CaseInsensitiveObjectWrapper& object)
{
    NodeInfoConfig nodeInfoConfig {};

    nodeInfoConfig.mProvisioningStatePath
        = object.GetValue<std::string>("provisioningStatePath", cDefaultProvisioningStatusPath);
    nodeInfoConfig.mCPUInfoPath = object.GetValue<std::string>("cpuInfoPath", cDefaultCPUInfoPath);
    nodeInfoConfig.mMemInfoPath = object.GetValue<std::string>("memInfoPath", cDefaultMemInfoPath);
    nodeInfoConfig.mNodeIDPath  = object.GetValue<std::string>("nodeIDPath", cDefaultNodeIDPath);
    nodeInfoConfig.mNodeName    = object.GetValue<std::string>("nodeName");
    nodeInfoConfig.mNodeType    = object.GetValue<std::string>("nodeType");
    nodeInfoConfig.mOSType      = object.GetValue<std::string>("osType");
    nodeInfoConfig.mMaxDMIPS    = object.GetValue<uint64_t>("maxDMIPS");

    if (object.Has("attrs")) {
        for (const auto& [key, value] : *object.Get("attrs").extract<Poco::JSON::Object::Ptr>()) {
            nodeInfoConfig.mAttrs.emplace(key, value.extract<std::string>());
        }
    }

    if (object.Has("partitions")) {
        nodeInfoConfig.mPartitions = aos::common::utils::GetArrayValue<PartitionInfoConfig>(
            object, "partitions", [](const Poco::Dynamic::Var& value) {
                return ParsePartitionInfoConfig(
                    aos::common::utils::CaseInsensitiveObjectWrapper(value.extract<Poco::JSON::Object::Ptr>()));
            });
    }

    return nodeInfoConfig;
}

/***********************************************************************************************************************
 * Public functions
 **********************************************************************************************************************/

aos::RetWithError<Config> ParseConfig(const std::string& filename)
{
    std::ifstream file(filename);

    if (!file.is_open()) {
        return {Config {}, aos::ErrorEnum::eNotFound};
    }

    Config config {};

    try {
        Poco::JSON::Parser                               parser;
        auto                                             result = parser.parse(file);
        aos::common::utils::CaseInsensitiveObjectWrapper object(result.extract<Poco::JSON::Object::Ptr>());

        config.mNodeInfo                  = ParseNodeInfoConfig(object.GetObject("nodeInfo"));
        config.mIAMPublicServerURL        = object.GetValue<std::string>("iamPublicServerURL");
        config.mIAMProtectedServerURL     = object.GetValue<std::string>("iamProtectedServerURL");
        config.mMainIAMPublicServerURL    = object.GetValue<std::string>("mainIAMPublicServerURL");
        config.mMainIAMProtectedServerURL = object.GetValue<std::string>("mainIAMProtectedServerURL");

        config.mCACert                   = object.GetValue<std::string>("caCert");
        config.mCertStorage              = object.GetValue<std::string>("certStorage");
        config.mWorkingDir               = object.GetValue<std::string>("workingDir");
        config.mMigrationPath            = object.GetValue<std::string>("migrationPath");
        config.mEnablePermissionsHandler = object.GetValue<bool>("enablePermissionsHandler");

        config.mStartProvisioningCmdArgs = aos::common::utils::GetArrayValue<std::string>(object,
            "startProvisioningCmdArgs", [](const Poco::Dynamic::Var& value) { return value.convert<std::string>(); });

        config.mDiskEncryptionCmdArgs = aos::common::utils::GetArrayValue<std::string>(object, "diskEncryptionCmdArgs",
            [](const Poco::Dynamic::Var& value) { return value.convert<std::string>(); });

        config.mFinishProvisioningCmdArgs = aos::common::utils::GetArrayValue<std::string>(object,
            "finishProvisioningCmdArgs", [](const Poco::Dynamic::Var& value) { return value.convert<std::string>(); });

        config.mDeprovisionCmdArgs = aos::common::utils::GetArrayValue<std::string>(
            object, "deprovisionCmdArgs", [](const Poco::Dynamic::Var& value) { return value.convert<std::string>(); });

        config.mCertModules = aos::common::utils::GetArrayValue<ModuleConfig>(
            object, "certModules", [](const Poco::Dynamic::Var& value) {
                return ParseModuleConfig(
                    aos::common::utils::CaseInsensitiveObjectWrapper(value.extract<Poco::JSON::Object::Ptr>()));
            });

        if (object.Has("identifier")) {
            config.mIdentifier = ParseIdentifier(object.GetObject("identifier"));
        }

        aos::Error err                          = aos::ErrorEnum::eNone;
        Tie(config.mNodeReconnectInterval, err) = aos::common::utils::ParseDuration(
            object.GetOptionalValue<std::string>("nodeReconnectInterval").value_or("10s"));
        if (!err.IsNone()) {
            return {{}, AOS_ERROR_WRAP(err)};
        }
    } catch (const std::exception& e) {
        LOG_ERR() << "Error parsing config: " << e.what();

        return {Config {}, aos::ErrorEnum::eInvalidArgument};
    }

    return config;
}

aos::RetWithError<PKCS11ModuleParams> ParsePKCS11ModuleParams(Poco::Dynamic::Var params)
{
    PKCS11ModuleParams moduleParams;

    try {
        aos::common::utils::CaseInsensitiveObjectWrapper object(params.extract<Poco::JSON::Object::Ptr>());

        moduleParams.mLibrary         = object.GetValue<std::string>("library");
        moduleParams.mSlotID          = object.GetOptionalValue<uint32_t>("slotID");
        moduleParams.mSlotIndex       = object.GetOptionalValue<int>("slotIndex");
        moduleParams.mTokenLabel      = object.GetValue<std::string>("tokenLabel");
        moduleParams.mUserPINPath     = object.GetValue<std::string>("userPinPath");
        moduleParams.mModulePathInURL = object.GetValue<bool>("modulePathInUrl");
        moduleParams.mUID             = object.GetOptionalValue<uint32_t>("uid").value_or(0);
        moduleParams.mGID             = object.GetOptionalValue<uint32_t>("gid").value_or(0);

    } catch (const std::exception& e) {
        LOG_ERR() << "Error parsing PKCS11 module params: " << e.what();

        return {PKCS11ModuleParams {}, aos::ErrorEnum::eInvalidArgument};
    }

    return moduleParams;
}

aos::RetWithError<VISIdentifierModuleParams> ParseVISIdentifierModuleParams(Poco::Dynamic::Var params)
{
    VISIdentifierModuleParams moduleParams;

    try {
        aos::common::utils::CaseInsensitiveObjectWrapper object(params.extract<Poco::JSON::Object::Ptr>());

        moduleParams.mVISServer        = object.GetValue<std::string>("visServer");
        moduleParams.mCaCertFile       = object.GetValue<std::string>("caCertFile");
        moduleParams.mWebSocketTimeout = object.GetValue<int>("webSocketTimeout");

    } catch (const std::exception& e) {
        LOG_ERR() << "Error parsing VIS identifier module params: " << e.what();

        return {VISIdentifierModuleParams {}, aos::ErrorEnum::eInvalidArgument};
    }

    return moduleParams;
}
