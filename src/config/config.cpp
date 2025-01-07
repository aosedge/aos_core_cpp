/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>

#include <Poco/JSON/Parser.h>

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

MonitoringConfig ParseMonitoringConfig(const common::utils::CaseInsensitiveObjectWrapper& object)
{
    MonitoringConfig config {};

    config.mSource = object.GetValue<std::string>("source");

    Error err                    = ErrorEnum::eNone;
    Tie(config.mPollPeriod, err) = common::utils::ParseDuration(
        object.GetOptionalValue<std::string>("pollPeriod").value_or(cDefaultMonitoringPollPeriod));
    AOS_ERROR_CHECK_AND_THROW("error parsing pollPeriod tag", err);

    Tie(config.mAverageWindow, err) = common::utils::ParseDuration(
        object.GetOptionalValue<std::string>("averageWindow").value_or(cDefaultMonitoringAverageWindow));
    AOS_ERROR_CHECK_AND_THROW("error parsing averageWindow tag", err);

    return config;
}

LoggingConfig ParseLoggingConfig(const common::utils::CaseInsensitiveObjectWrapper& object)
{
    return LoggingConfig {
        object.GetValue<uint64_t>("maxPartSize"),
        object.GetValue<uint64_t>("maxPartCount"),
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

std::vector<HostInfoConfig> ParseHostsConfig(const common::utils::CaseInsensitiveObjectWrapper& object)
{
    return common::utils::GetArrayValue<HostInfoConfig>(object, "hosts", [](const Poco::Dynamic::Var& value) {
        common::utils::CaseInsensitiveObjectWrapper item(value);

        return HostInfoConfig {
            item.GetValue<std::string>("ip"),
            item.GetValue<std::string>("hostname"),
        };
    });
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

        config.mCertStorage = object.GetOptionalValue<std::string>("certStorage").value_or("/var/aos/crypt/sm/");
        config.mCMServerURL = object.GetValue<std::string>("cmServerURL");
        config.mIAMProtectedServerURL = object.GetValue<std::string>("iamProtectedServerURL");

        config.mStorageDir
            = object.GetOptionalValue<std::string>("storageDir").value_or(JoinPath(config.mWorkingDir, "storages"));

        config.mStateDir
            = object.GetOptionalValue<std::string>("stateDir").value_or(JoinPath(config.mWorkingDir, "states"));

        config.mServicesPartLimit = object.GetValue<uint32_t>("servicesPartLimit");

        config.mLayersPartLimit = object.GetValue<uint32_t>("layersPartLimit");

        config.mExtractDir
            = object.GetOptionalValue<std::string>("extractDir").value_or(JoinPath(config.mWorkingDir, "extracts"));

        config.mNodeConfigFile = object.GetOptionalValue<std::string>("nodeConfigFile")
                                     .value_or(JoinPath(config.mWorkingDir, "aos_node.cfg"));

        Error err = ErrorEnum::eNone;

        Tie(config.mServiceHealthCheckTimeout, err) = common::utils::ParseDuration(
            object.GetOptionalValue<std::string>("serviceHealthCheckTimeout").value_or(cDefaultHealthCheckTimeout));
        AOS_ERROR_CHECK_AND_THROW("error parsing serviceHealthCheckTimeout tag", err);

        Tie(config.mCMReconnectTimeout, err) = common::utils::ParseDuration(
            object.GetOptionalValue<std::string>("cmReconnectTimeout").value_or(cDefaultCMReconnectTimeout));

        if (object.Has("monitoring")) {
            config.mMonitoring = ParseMonitoringConfig(object.GetObject("monitoring"));
        }

        if (object.Has("logging")) {
            config.mLogging = ParseLoggingConfig(object.GetObject("logging"));
        }

        if (object.Has("journalAlerts")) {
            config.mJournalAlerts = ParseJournalAlertsConfig(object.GetObject("journalAlerts"));
        }

        if (object.Has("hostBinds")) {
            config.mHostBinds = common::utils::GetArrayValue<std::string>(object, "hostBinds");
        }

        if (object.Has("hosts")) {
            config.mHosts = ParseHostsConfig(object);
        }

        if (object.Has("migration")) {
            config.mMigration = ParseMigrationConfig(config.mWorkingDir, object.GetObject("migration"));
        }
    } catch (const std::exception& e) {
        LOG_ERR() << "Error parsing config: " << e.what();

        return {Config {}, ErrorEnum::eFailed};
    }

    return {config, ErrorEnum::eNone};
}

} // namespace aos::sm::config
