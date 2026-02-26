/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>

#include <Poco/JSON/Parser.h>

#include <core/common/tools/fs.hpp>
#include <core/common/types/log.hpp>

#include <common/utils/exception.hpp>
#include <common/utils/filesystem.hpp>
#include <common/utils/json.hpp>
#include <sm/logger/logmodule.hpp>

#include "config.hpp"

/***********************************************************************************************************************
 * Constants
 **********************************************************************************************************************/

constexpr auto cDefaultUpdateItemTTL        = "30d";
constexpr auto cDefaultRemoveOutdatedPeriod = "24h";
constexpr auto cDefaultHealthCheckTimeout   = "35s";
constexpr auto cDefaultCMReconnectTimeout   = "10s";
const auto     cEmptyObject                 = Poco::makeShared<Poco::JSON::Object>();
constexpr auto cResourceConfigFileName      = "/etc/aos/resources.cfg";

namespace aos::sm::config {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

namespace {

void ParseLoggingConfig(const common::utils::CaseInsensitiveObjectWrapper& object, logging::Config& config)
{
    config.mMaxPartSize  = object.GetValue<uint64_t>("maxPartSize", cLogContentLen);
    config.mMaxPartCount = object.GetValue<uint64_t>("maxPartCount", 80);
}

void ParseIAMClientConfig(const common::utils::CaseInsensitiveObjectWrapper& object, common::iamclient::Config& config)
{
    config.mIAMPublicServerURL = object.GetValue<std::string>("iamPublicServerURL");
    config.mCACert             = object.GetValue<std::string>("caCert");
}

void ParseSMClientConfig(const common::utils::CaseInsensitiveObjectWrapper& object, smclient::Config& config)
{
    config.mCertStorage = object.GetValue<std::string>("certStorage").c_str();
    config.mCMServerURL = object.GetValue<std::string>("cmServerURL");

    Error err = ErrorEnum::eNone;

    Tie(config.mCMReconnectTimeout, err)
        = common::utils::ParseDuration(object.GetValue<std::string>("cmReconnectTimeout", cDefaultCMReconnectTimeout));
    AOS_ERROR_CHECK_AND_THROW(err, "error parsing cmReconnectTimeout tag");
};

void ParseImageManagerConfig(const common::utils::CaseInsensitiveObjectWrapper& object, const std::string& workingDir,
    imagemanager::Config& config)
{
    Error err;

    err = config.mImagePath.Assign(
        object.GetValue<std::string>("imagePath", std::filesystem::path(workingDir) / "images").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "error parsing imagePath tag");

    config.mPartLimit = object.GetValue<size_t>("imagesPartLimit", 0);

    Tie(config.mUpdateItemTTL, err)
        = common::utils::ParseDuration(object.GetValue<std::string>("updateItemTtl", cDefaultUpdateItemTTL));
    AOS_ERROR_CHECK_AND_THROW(err, "error parsing updateItemTtl tag");

    Tie(config.mRemoveOutdatedPeriod, err) = common::utils::ParseDuration(
        object.GetValue<std::string>("removeOutdatedPeriod", cDefaultRemoveOutdatedPeriod));
    AOS_ERROR_CHECK_AND_THROW(err, "error parsing removeOutdatedPeriod tag");
}

launcher::RuntimeConfig ParseRuntimeConfig(
    const common::utils::CaseInsensitiveObjectWrapper& object, const std::string& workingDir)
{
    auto config = launcher::RuntimeConfig {object.GetValue<std::string>("plugin"), object.GetValue<std::string>("type"),
        object.GetValue<bool>("isComponent", false), workingDir, nullptr};

    config.mConfig = object.Has("config") ? object.GetObject("config") : cEmptyObject;

    return config;
}

} // namespace

/***********************************************************************************************************************
 * Public functions
 **********************************************************************************************************************/

Error ParseConfig(const std::string& filename, Config& config)
{
    std::ifstream file(filename);

    if (!file.is_open()) {
        return ErrorEnum::eNotFound;
    }

    try {
        Poco::JSON::Parser                          parser;
        auto                                        result = parser.parse(file);
        common::utils::CaseInsensitiveObjectWrapper object(result);

        config.mWorkingDir = object.GetValue<std::string>("workingDir");

        ParseIAMClientConfig(object, config.mIAMClientConfig);
        ParseSMClientConfig(object, config.mSMClientConfig);

        config.mCertStorage = object.GetOptionalValue<std::string>("certStorage").value_or("/var/aos/crypt/sm/");
        config.mIAMProtectedServerURL = object.GetValue<std::string>("iamProtectedServerURL");

        config.mNodeConfigFile = object.GetOptionalValue<std::string>("nodeConfigFile")
                                     .value_or(common::utils::JoinPath(config.mWorkingDir, "aos_node.cfg"));

        config.mResourcesConfigFile
            = object.GetOptionalValue<std::string>("resourcesConfigFile").value_or(cResourceConfigFileName);

        auto empty = common::utils::CaseInsensitiveObjectWrapper(Poco::makeShared<Poco::JSON::Object>());

        common::config::ParseMonitoringConfig(
            object.Has("monitoring") ? object.GetObject("monitoring") : empty, config.mMonitoring);

        auto imageManager  = object.Has("imageManager") ? object.GetObject("imageManager") : empty;
        auto logging       = object.Has("logging") ? object.GetObject("logging") : empty;
        auto journalAlerts = object.Has("journalAlerts") ? object.GetObject("journalAlerts") : empty;
        auto migration     = object.Has("migration") ? object.GetObject("migration") : empty;

        ParseImageManagerConfig(imageManager, config.mWorkingDir, config.mImageManager);
        ParseLoggingConfig(logging, config.mLogging);
        common::config::ParseJournalAlertsConfig(journalAlerts, config.mJournalAlerts);
        common::config::ParseMigrationConfig(migration, "/usr/share/aos/servicemanager/migration",
            common::utils::JoinPath(config.mWorkingDir, "mergedMigration"), config.mMigration);

        config.mLauncher.mRuntimes = common::utils::GetArrayValue<launcher::RuntimeConfig>(
            object, "runtimes", [&](const Poco::Dynamic::Var& value) {
                return ParseRuntimeConfig(common::utils::CaseInsensitiveObjectWrapper(value), config.mWorkingDir);
            });
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::sm::config
