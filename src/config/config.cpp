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
#include "log.hpp"

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

template <typename T, typename ParserFunc>
std::vector<T> GetArrayValue(
    const aos::common::utils::CaseInsensitiveObjectWrapper& object, const std::string& key, ParserFunc parserFunc)
{
    std::vector<T> result;

    if (!object.Has(key)) {
        return result;
    }

    Poco::JSON::Array::Ptr array = object.GetArray(key);

    std::transform(array->begin(), array->end(), std::back_inserter(result), parserFunc);

    return result;
}

static Identifier ParseIdentifier(const aos::common::utils::CaseInsensitiveObjectWrapper& object)
{
    return Identifier {object.GetValue<std::string>("plugin"), object.Get("params")};
}

static RemoteIAM ParseRemoteIAM(const aos::common::utils::CaseInsensitiveObjectWrapper& object)
{
    aos::common::utils::Duration duration {};
    auto                         requestTimeoutString = object.GetValue<std::string>("requestTimeout");

    if (!requestTimeoutString.empty()) {
        auto ret = aos::common::utils::ParseDuration(requestTimeoutString);

        if (!ret.mError.IsNone()) {
            throw std::runtime_error("Error parsing duration");
        }

        duration = ret.mValue;
    }

    return RemoteIAM {object.GetValue<std::string>("nodeID"), object.GetValue<std::string>("url"), duration};
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
        object.Get("params"),
    };
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

        config.mIAMPublicServerURL       = object.GetValue<std::string>("iamPublicServerURL");
        config.mIAMProtectedServerURL    = object.GetValue<std::string>("iamProtectedServerURL");
        config.mNodeID                   = object.GetValue<std::string>("nodeID");
        config.mNodeType                 = object.GetValue<std::string>("nodeType");
        config.mCACert                   = object.GetValue<std::string>("caCert");
        config.mCertStorage              = object.GetValue<std::string>("certStorage");
        config.mWorkingDir               = object.GetValue<std::string>("workingDir");
        config.mMigrationPath            = object.GetValue<std::string>("migrationPath");
        config.mEnablePermissionsHandler = object.GetValue<bool>("enablePermissionsHandler");

        config.mFinishProvisioningCmdArgs = aos::common::utils::GetArrayValue<std::string>(object,
            "finishProvisioningCmdArgs", [](const Poco::Dynamic::Var& value) { return value.convert<std::string>(); });

        config.mDiskEncryptionCmdArgs = aos::common::utils::GetArrayValue<std::string>(object, "diskEncryptionCmdArgs",
            [](const Poco::Dynamic::Var& value) { return value.convert<std::string>(); });

        config.mCertModules = aos::common::utils::GetArrayValue<ModuleConfig>(
            object, "certModules", [](const Poco::Dynamic::Var& value) {
                return ParseModuleConfig(
                    aos::common::utils::CaseInsensitiveObjectWrapper(value.extract<Poco::JSON::Object::Ptr>()));
            });

        config.mRemoteIAMs
            = aos::common::utils::GetArrayValue<RemoteIAM>(object, "remoteIAMs", [](const Poco::Dynamic::Var& value) {
                  return ParseRemoteIAM(
                      aos::common::utils::CaseInsensitiveObjectWrapper(value.extract<Poco::JSON::Object::Ptr>()));
              });

        if (object.Has("identifier")) {
            config.mIdentifier = ParseIdentifier(object.GetObject("identifier"));
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
