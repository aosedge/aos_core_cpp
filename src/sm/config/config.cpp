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
#include <common/utils/json.hpp>
#include <sm/logger/logmodule.hpp>

#include "config.hpp"

/***********************************************************************************************************************
 * Constants
 **********************************************************************************************************************/

constexpr auto cDefaultServiceTTLDays     = "30d";
constexpr auto cDefaultLayerTTLDays       = "30d";
constexpr auto cDefaultHealthCheckTimeout = "35s";
constexpr auto cDefaultCMReconnectTimeout = "10s";

namespace aos::sm::config {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

namespace {

std::filesystem::path JoinPath(const std::string& base, const std::string& entry)
{
    auto path = std::filesystem::path(base);

    path /= entry;

    return path;
}

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

void ParseLauncherConfig(const common::utils::CaseInsensitiveObjectWrapper& object, launcher::Config& config)
{
    if (object.Has("runtimes")) {
        auto runtimesObject = object.GetObject("runtimes");

        for (const auto& name : runtimesObject.GetNames()) {
            auto runtimeObject = common::utils::CaseInsensitiveObjectWrapper(runtimesObject.GetObject(name));

            launcher::RuntimeConfig runtimeConfig;

            runtimeConfig.mType = runtimeObject.GetValue<std::string>("type");

            if (runtimeObject.Has("config")) {
                runtimeConfig.mConfig = runtimeObject.GetObject("config");
            }

            config.mRuntimes.emplace(name, std::move(runtimeConfig));
        }
    }
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
                                     .value_or(JoinPath(config.mWorkingDir, "aos_node.cfg"));

        auto empty = common::utils::CaseInsensitiveObjectWrapper(Poco::makeShared<Poco::JSON::Object>());

        common::config::ParseMonitoringConfig(
            object.Has("monitoring") ? object.GetObject("monitoring") : empty, config.mMonitoring);

        auto launcher      = object.Has("launcher") ? object.GetObject("launcher") : empty;
        auto logging       = object.Has("logging") ? object.GetObject("logging") : empty;
        auto journalAlerts = object.Has("journalAlerts") ? object.GetObject("journalAlerts") : empty;
        auto migration     = object.Has("migration") ? object.GetObject("migration") : empty;

        ParseLauncherConfig(launcher, config.mLauncher);
        ParseLoggingConfig(logging, config.mLogging);
        common::config::ParseJournalAlertsConfig(journalAlerts, config.mJournalAlerts);
        common::config::ParseMigrationConfig(migration, "/usr/share/aos/servicemanager/migration",
            JoinPath(config.mWorkingDir, "mergedMigration"), config.mMigration);
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::sm::config
