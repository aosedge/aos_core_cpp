/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>

#include <Poco/JSON/Parser.h>

#include <aos/common/cloudprotocol/log.hpp>
#include <aos/common/tools/fs.hpp>

#include <utils/exception.hpp>
#include <utils/json.hpp>

#include "config.hpp"
#include "logger/logmodule.hpp"

/***********************************************************************************************************************
 * Constants
 **********************************************************************************************************************/

constexpr auto cDefaultServiceTTLDays          = "30d";
constexpr auto cDefaultLayerTTLDays            = "30d";
constexpr auto cDefaultHealthCheckTimeout      = "35s";
constexpr auto cDefaultCMReconnectTimeout      = "10s";
constexpr auto cDefaultMonitoringPollPeriod    = "35s";
constexpr auto cDefaultMonitoringAverageWindow = "35s";
constexpr auto cDefaultServiceAlertPriority    = 4;
constexpr auto cDefaultSystemAlertPriority     = 3;
constexpr auto cMaxAlertPriorityLevel          = 7;
constexpr auto cMinAlertPriorityLevel          = 0;

namespace aos::sm::config {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

namespace {

monitoring::Config ParseMonitoringConfig(const common::utils::CaseInsensitiveObjectWrapper& object)
{
    monitoring::Config config {};

    const auto pollPeriod = object.Has("monitoring")
        ? object.GetObject("monitoring").GetValue<std::string>("pollPeriod", cDefaultMonitoringPollPeriod)
        : cDefaultMonitoringPollPeriod;

    const auto averageWindow = object.Has("monitoring")
        ? object.GetObject("monitoring").GetValue<std::string>("averageWindow", cDefaultMonitoringAverageWindow)
        : cDefaultMonitoringAverageWindow;

    Error                   err = ErrorEnum::eNone;
    common::utils::Duration duration;

    Tie(duration, err) = common::utils::ParseDuration(pollPeriod);
    AOS_ERROR_CHECK_AND_THROW("error parsing pollPeriod tag", err);

    config.mPollPeriod = duration.count();

    Tie(duration, err) = common::utils::ParseDuration(averageWindow);
    AOS_ERROR_CHECK_AND_THROW("error parsing averageWindow tag", err);

    config.mAverageWindow = duration.count();

    return config;
}

LoggingConfig ParseLoggingConfig(const common::utils::CaseInsensitiveObjectWrapper& object)
{
    return LoggingConfig {
        object.GetValue<uint64_t>("maxPartSize", cloudprotocol::cLogContentLen),
        object.GetValue<uint64_t>("maxPartCount", 80),
    };
}

JournalAlertsConfig ParseJournalAlertsConfig(const common::utils::CaseInsensitiveObjectWrapper& object)
{
    JournalAlertsConfig config {};

    config.mFilter = common::utils::GetArrayValue<std::string>(object, "filter");

    config.mServiceAlertPriority
        = object.GetOptionalValue<int>("serviceAlertPriority").value_or(cDefaultServiceAlertPriority);
    if (config.mServiceAlertPriority > cMaxAlertPriorityLevel
        || config.mServiceAlertPriority < cMinAlertPriorityLevel) {
        config.mServiceAlertPriority = cDefaultServiceAlertPriority;

        LOG_WRN() << "Default value is set for service alert priority: value=" << cDefaultServiceAlertPriority;
    }

    config.mSystemAlertPriority
        = object.GetOptionalValue<int>("systemAlertPriority").value_or(cDefaultSystemAlertPriority);
    if (config.mSystemAlertPriority > cMaxAlertPriorityLevel || config.mSystemAlertPriority < cMinAlertPriorityLevel) {
        config.mSystemAlertPriority = cDefaultSystemAlertPriority;

        LOG_WRN() << "Default value is set for system alert priority: value=" << cDefaultServiceAlertPriority;
    }

    return config;
}

Host ParseHostConfig(const common::utils::CaseInsensitiveObjectWrapper& object)
{
    const auto ip       = object.GetValue<std::string>("ip");
    const auto hostname = object.GetValue<std::string>("hostname");

    return Host {
        ip.c_str(),
        hostname.c_str(),
    };
}

MigrationConfig ParseMigrationConfig(
    const std::string& workDir, const common::utils::CaseInsensitiveObjectWrapper& object)
{
    return MigrationConfig {
        object.GetOptionalValue<std::string>("migrationPath").value_or("/usr/share/aos/servicemanager/migration"),
        object.GetOptionalValue<std::string>("mergedMigrationPath")
            .value_or(FS::JoinPath(workDir.c_str(), "mergedMigration").CStr()),
    };
}

common::iamclient::Config ParseIAMClientConfig(const common::utils::CaseInsensitiveObjectWrapper& object)
{
    return common::iamclient::Config {
        object.GetValue<std::string>("iamPublicServerURL"),
        object.GetValue<std::string>("caCert"),
    };
};

std::filesystem::path JoinPath(const std::string& base, const std::string& entry)
{
    auto path = std::filesystem::path(base);

    path /= entry;

    return path;
}

sm::servicemanager::Config ParseServiceManagerConfig(
    const std::string& workingDir, const common::utils::CaseInsensitiveObjectWrapper& object)
{
    const auto servicesDir = object.GetValue<std::string>("servicesDir", JoinPath(workingDir, "services"));
    const auto downloadDir = object.GetValue<std::string>("downloadDir", JoinPath(workingDir, "downloads"));
    const auto serviceTTL  = object.GetValue<std::string>("serviceTTL", cDefaultServiceTTLDays);

    const auto [ttl, err] = common::utils::ParseDuration(serviceTTL);
    AOS_ERROR_CHECK_AND_THROW("error parsing serviceTTL tag", err);

    return sm::servicemanager::Config {
        servicesDir.c_str(),
        downloadDir.c_str(),
        ttl.count(),
    };
}

sm::layermanager::Config ParseLayerManagerConfig(
    const std::string& workingDir, const common::utils::CaseInsensitiveObjectWrapper& object)
{
    const auto layersDir   = object.GetValue<std::string>("layersDir", JoinPath(workingDir, "layers"));
    const auto downloadDir = object.GetValue<std::string>("downloadDir", JoinPath(workingDir, "downloads"));
    const auto ttlStr      = object.GetValue<std::string>("layerTTL", cDefaultLayerTTLDays);

    const auto [ttl, err] = common::utils::ParseDuration(ttlStr);
    AOS_ERROR_CHECK_AND_THROW("error parsing layerTTL tag", err);

    return sm::layermanager::Config {
        layersDir.c_str(),
        downloadDir.c_str(),
        ttl.count(),
    };
}

sm::launcher::Config ParseLauncherConfig(
    const std::string& workingDir, const common::utils::CaseInsensitiveObjectWrapper& object)
{
    const auto storageDir = object.GetValue<std::string>("storageDir", JoinPath(workingDir, "storages"));
    const auto stateDir   = object.GetValue<std::string>("stateDir", JoinPath(workingDir, "states"));

    sm::launcher::Config config {};

    config.mStorageDir = storageDir.c_str();
    config.mStateDir   = stateDir.c_str();
    config.mWorkDir    = workingDir.c_str();

    const auto hostBinds = common::utils::GetArrayValue<std::string>(object, "hostBinds");
    for (const auto& hostBind : hostBinds) {
        auto err = config.mHostBinds.EmplaceBack(hostBind.c_str());
        AOS_ERROR_CHECK_AND_THROW("error parsing hostBinds tag", err);
    }

    const auto hosts = common::utils::GetArrayValue<Host>(object, "hosts",
        [](const auto& val) { return ParseHostConfig(common::utils::CaseInsensitiveObjectWrapper(val)); });
    for (const auto& host : hosts) {
        auto err = config.mHosts.EmplaceBack(host);
        AOS_ERROR_CHECK_AND_THROW("error parsing hosts tag", err);
    }

    return config;
}

smclient::Config ParseSMClientConfig(const common::utils::CaseInsensitiveObjectWrapper& object)
{
    smclient::Config config;

    config.mCertStorage = object.GetValue<std::string>("certStorage");
    config.mCMServerURL = object.GetValue<std::string>("cmServerURL");

    Error err = ErrorEnum::eNone;

    const auto reconnectTimeout = object.GetValue<std::string>("cmReconnectTimeout", cDefaultCMReconnectTimeout);

    Tie(config.mCMReconnectTimeout, err) = common::utils::ParseDuration(reconnectTimeout);
    AOS_ERROR_CHECK_AND_THROW("error parsing cmReconnectTimeout tag", err);

    return config;
};

} // namespace

/***********************************************************************************************************************
 * Public functions
 **********************************************************************************************************************/

RetWithError<Config> ParseConfig(const std::string& filename)
{
    std::ifstream file(filename);

    if (!file.is_open()) {
        return {Config {}, ErrorEnum::eNotFound};
    }

    Config config {};

    try {
        Poco::JSON::Parser                          parser;
        auto                                        result = parser.parse(file);
        common::utils::CaseInsensitiveObjectWrapper object(result);

        config.mWorkingDir = object.GetValue<std::string>("workingDir");

        config.mIAMClientConfig      = ParseIAMClientConfig(object);
        config.mLayerManagerConfig   = ParseLayerManagerConfig(config.mWorkingDir, object);
        config.mServiceManagerConfig = ParseServiceManagerConfig(config.mWorkingDir, object);
        config.mLauncherConfig       = ParseLauncherConfig(config.mWorkingDir, object);
        config.mSMClientConfig       = ParseSMClientConfig(object);

        config.mCertStorage = object.GetOptionalValue<std::string>("certStorage").value_or("/var/aos/crypt/sm/");
        config.mIAMProtectedServerURL = object.GetValue<std::string>("iamProtectedServerURL");

        config.mServicesPartLimit = object.GetValue<uint32_t>("servicesPartLimit");

        config.mLayersPartLimit = object.GetValue<uint32_t>("layersPartLimit");

        config.mNodeConfigFile = object.GetOptionalValue<std::string>("nodeConfigFile")
                                     .value_or(JoinPath(config.mWorkingDir, "aos_node.cfg"));

        config.mMonitoring = ParseMonitoringConfig(object);

        auto empty = common::utils::CaseInsensitiveObjectWrapper(Poco::makeShared<Poco::JSON::Object>());

        if (object.Has("logging")) {
            config.mLogging = ParseLoggingConfig(object.GetObject("logging"));
        } else {
            config.mLogging = ParseLoggingConfig(empty);
        }

        if (object.Has("journalAlerts")) {
            config.mJournalAlerts = ParseJournalAlertsConfig(object.GetObject("journalAlerts"));
        } else {
            config.mJournalAlerts = ParseJournalAlertsConfig(empty);
        }

        if (object.Has("migration")) {
            config.mMigration = ParseMigrationConfig(config.mWorkingDir, object.GetObject("migration"));
        } else {
            config.mMigration = ParseMigrationConfig(config.mWorkingDir, empty);
        }
    } catch (const std::exception& e) {
        return {{}, common::utils::ToAosError(e)};
    }

    return {config, ErrorEnum::eNone};
}

} // namespace aos::sm::config
