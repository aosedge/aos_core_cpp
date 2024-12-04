/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <fstream>

#include <Poco/JSON/Parser.h>

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

static MonitoringConfig ParseMonitoringConfig(const common::utils::CaseInsensitiveObjectWrapper& object)
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

static LoggingConfig ParseLoggingConfig(const common::utils::CaseInsensitiveObjectWrapper& object)
{
    return LoggingConfig {
        object.GetValue<uint64_t>("maxPartSize"),
        object.GetValue<uint64_t>("maxPartCount"),
    };
}

static JournalAlertsConfig ParseJournalAlertsConfig(const common::utils::CaseInsensitiveObjectWrapper& object)
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

static std::vector<HostInfoConfig> ParseHostsConfig(const common::utils::CaseInsensitiveObjectWrapper& object)
{
    return common::utils::GetArrayValue<HostInfoConfig>(object, "hosts", [](const Poco::Dynamic::Var& value) {
        common::utils::CaseInsensitiveObjectWrapper item(value);

        return HostInfoConfig {
            item.GetValue<std::string>("ip"),
            item.GetValue<std::string>("hostname"),
        };
    });
}

static MigrationConfig ParseMigrationConfig(const common::utils::CaseInsensitiveObjectWrapper& object)
{
    return MigrationConfig {
        object.GetValue<std::string>("migrationPath"),
        object.GetValue<std::string>("mergedMigrationPath"),
    };
}

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

        config.mCACert                = object.GetValue<std::string>("caCert");
        config.mCertStorage           = object.GetValue<std::string>("certStorage");
        config.mCMServerURL           = object.GetValue<std::string>("cmServerURL");
        config.mIAMPublicServerURL    = object.GetValue<std::string>("iamPublicServerURL");
        config.mIAMProtectedServerURL = object.GetValue<std::string>("iamProtectedServerURL");
        config.mWorkingDir            = object.GetValue<std::string>("workingDir");
        config.mStorageDir            = object.GetValue<std::string>("storageDir");
        config.mStateDir              = object.GetValue<std::string>("stateDir");

        config.mServicesDir       = object.GetValue<std::string>("servicesDir");
        config.mServicesPartLimit = object.GetValue<uint32_t>("servicesPartLimit");

        config.mLayersDir       = object.GetValue<std::string>("layersDir");
        config.mLayersPartLimit = object.GetValue<uint32_t>("layersPartLimit");

        config.mDownloadDir    = object.GetValue<std::string>("downloadDir");
        config.mExtractDir     = object.GetValue<std::string>("extractDir");
        config.mNodeConfigFile = object.GetValue<std::string>("nodeConfigFile");

        Error err = ErrorEnum::eNone;

        Tie(config.mServiceTTL, err) = common::utils::ParseDuration(
            object.GetOptionalValue<std::string>("serviceTTL").value_or(cDefaultServiceTTLDays));
        AOS_ERROR_CHECK_AND_THROW("error parsing serviceTTL tag", err);

        Tie(config.mLayerTTL, err) = common::utils::ParseDuration(
            object.GetOptionalValue<std::string>("layerTTL").value_or(cDefaultLayerTTLDays));
        AOS_ERROR_CHECK_AND_THROW("error parsing layerTTL tag", err);

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
            config.mMigration = ParseMigrationConfig(object.GetObject("migration"));
        }
    } catch (const std::exception& e) {
        LOG_ERR() << "Error parsing config: " << e.what();

        return {Config {}, ErrorEnum::eFailed};
    }

    return {config, ErrorEnum::eNone};
}

} // namespace aos::sm::config
