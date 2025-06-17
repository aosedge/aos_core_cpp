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

#include <common/utils/exception.hpp>
#include <common/utils/json.hpp>
#include <sm/logger/logmodule.hpp>

#include "config.hpp"

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

std::filesystem::path JoinPath(const std::string& base, const std::string& entry)
{
    auto path = std::filesystem::path(base);

    path /= entry;

    return path;
}

void ParseMonitoringConfig(const common::utils::CaseInsensitiveObjectWrapper& object, monitoring::Config& config)
{
    const auto pollPeriod = object.Has("monitoring")
        ? object.GetObject("monitoring").GetValue<std::string>("pollPeriod", cDefaultMonitoringPollPeriod)
        : cDefaultMonitoringPollPeriod;

    const auto averageWindow = object.Has("monitoring")
        ? object.GetObject("monitoring").GetValue<std::string>("averageWindow", cDefaultMonitoringAverageWindow)
        : cDefaultMonitoringAverageWindow;

    Error err = ErrorEnum::eNone;

    Tie(config.mPollPeriod, err) = common::utils::ParseDuration(pollPeriod);
    AOS_ERROR_CHECK_AND_THROW(err, "error parsing pollPeriod tag");

    Tie(config.mAverageWindow, err) = common::utils::ParseDuration(averageWindow);
    AOS_ERROR_CHECK_AND_THROW(err, "error parsing averageWindow tag");
}

void ParseLoggingConfig(const common::utils::CaseInsensitiveObjectWrapper& object, common::logprovider::Config& config)
{
    config.mMaxPartSize  = object.GetValue<uint64_t>("maxPartSize", cloudprotocol::cLogContentLen);
    config.mMaxPartCount = object.GetValue<uint64_t>("maxPartCount", 80);
}

void ParseJournalAlertsConfig(const common::utils::CaseInsensitiveObjectWrapper& object, JournalAlertsConfig& config)
{
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

void ParseMigrationConfig(
    const common::utils::CaseInsensitiveObjectWrapper& object, const std::string& workingDir, MigrationConfig& config)
{
    config.mMigrationPath
        = object.GetOptionalValue<std::string>("migrationPath").value_or("/usr/share/aos/servicemanager/migration");
    config.mMergedMigrationPath = object.GetOptionalValue<std::string>("mergedMigrationPath")
                                      .value_or(JoinPath(workingDir, "mergedMigration").c_str());
}

void ParseIAMClientConfig(const common::utils::CaseInsensitiveObjectWrapper& object, common::iamclient::Config& config)
{
    config.mIAMPublicServerURL = object.GetValue<std::string>("iamPublicServerURL");
    config.mCACert             = object.GetValue<std::string>("caCert");
}

void ParseServiceManagerConfig(const common::utils::CaseInsensitiveObjectWrapper& object, const std::string& workingDir,
    sm::servicemanager::Config& config)
{
    config.mServicesDir = object.GetValue<std::string>("servicesDir", JoinPath(workingDir, "services")).c_str();
    config.mDownloadDir = object.GetValue<std::string>("downloadDir", JoinPath(workingDir, "downloads")).c_str();
    config.mPartLimit   = object.GetValue<size_t>("servicesPartLimit", 0);

    Error err = ErrorEnum::eNone;

    Tie(config.mTTL, err)
        = common::utils::ParseDuration(object.GetValue<std::string>("serviceTTL", cDefaultServiceTTLDays));
    AOS_ERROR_CHECK_AND_THROW(err, "error parsing serviceTTL tag");

    auto removeOutdatedPeriod = object.GetOptionalValue<std::string>("removeOutdatedPeriod");
    if (removeOutdatedPeriod.has_value()) {
        Tie(config.mRemoveOutdatedPeriod, err) = common::utils::ParseDuration(removeOutdatedPeriod.value());
        AOS_ERROR_CHECK_AND_THROW(err, "error parsing removeOutdatedPeriod tag");
    }
}

void ParseLayerManagerConfig(const common::utils::CaseInsensitiveObjectWrapper& object, const std::string& workingDir,
    sm::layermanager::Config& config)
{
    config.mLayersDir   = object.GetValue<std::string>("layersDir", JoinPath(workingDir, "layers")).c_str();
    config.mDownloadDir = object.GetValue<std::string>("downloadDir", JoinPath(workingDir, "downloads")).c_str();
    config.mPartLimit   = object.GetValue<size_t>("layersPartLimit", 0);

    Error err = ErrorEnum::eNone;

    Tie(config.mTTL, err)
        = common::utils::ParseDuration(object.GetValue<std::string>("layerTTL", cDefaultLayerTTLDays));
    AOS_ERROR_CHECK_AND_THROW(err, "error parsing layerTTL tag");

    auto removeOutdatedPeriod = object.GetOptionalValue<std::string>("removeOutdatedPeriod");
    if (removeOutdatedPeriod.has_value()) {
        Tie(config.mRemoveOutdatedPeriod, err) = common::utils::ParseDuration(removeOutdatedPeriod.value());
        AOS_ERROR_CHECK_AND_THROW(err, "error parsing removeOutdatedPeriod tag");
    }
}

void ParseLauncherConfig(const common::utils::CaseInsensitiveObjectWrapper& object, const std::string& workingDir,
    sm::launcher::Config& config)
{
    config.mStorageDir = object.GetValue<std::string>("storageDir", JoinPath(workingDir, "storages")).c_str();
    config.mStateDir   = object.GetValue<std::string>("stateDir", JoinPath(workingDir, "states")).c_str();
    config.mWorkDir    = workingDir.c_str();

    const auto hostBinds = common::utils::GetArrayValue<std::string>(object, "hostBinds");

    for (const auto& hostBind : hostBinds) {
        auto err = config.mHostBinds.EmplaceBack(hostBind.c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "error parsing hostBinds tag");
    }

    const auto hosts = common::utils::GetArrayValue<Host>(object, "hosts",
        [](const auto& val) { return ParseHostConfig(common::utils::CaseInsensitiveObjectWrapper(val)); });
    for (const auto& host : hosts) {
        auto err = config.mHosts.EmplaceBack(host);
        AOS_ERROR_CHECK_AND_THROW(err, "error parsing hosts tag");
    }

    auto removeOutdatedPeriod = object.GetOptionalValue<std::string>("removeOutdatedPeriod");
    if (removeOutdatedPeriod.has_value()) {
        Error err = ErrorEnum::eNone;

        Tie(config.mRemoveOutdatedPeriod, err) = common::utils::ParseDuration(removeOutdatedPeriod.value());
        AOS_ERROR_CHECK_AND_THROW(err, "error parsing removeOutdatedPeriod tag");
    }
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
        ParseLayerManagerConfig(object, config.mWorkingDir, config.mLayerManagerConfig);
        ParseServiceManagerConfig(object, config.mWorkingDir, config.mServiceManagerConfig);
        ParseLauncherConfig(object, config.mWorkingDir, config.mLauncherConfig);
        ParseSMClientConfig(object, config.mSMClientConfig);

        config.mCertStorage = object.GetOptionalValue<std::string>("certStorage").value_or("/var/aos/crypt/sm/");
        config.mIAMProtectedServerURL = object.GetValue<std::string>("iamProtectedServerURL");

        config.mServicesPartLimit = object.GetValue<uint32_t>("servicesPartLimit");

        config.mLayersPartLimit = object.GetValue<uint32_t>("layersPartLimit");

        config.mNodeConfigFile = object.GetOptionalValue<std::string>("nodeConfigFile")
                                     .value_or(JoinPath(config.mWorkingDir, "aos_node.cfg"));

        ParseMonitoringConfig(object, config.mMonitoring);

        auto empty         = common::utils::CaseInsensitiveObjectWrapper(Poco::makeShared<Poco::JSON::Object>());
        auto logging       = object.Has("logging") ? object.GetObject("logging") : empty;
        auto journalAlerts = object.Has("journalAlerts") ? object.GetObject("journalAlerts") : empty;
        auto migration     = object.Has("migration") ? object.GetObject("migration") : empty;

        ParseLoggingConfig(logging, config.mLogging);
        ParseJournalAlertsConfig(journalAlerts, config.mJournalAlerts);
        ParseMigrationConfig(migration, config.mWorkingDir, config.mMigration);
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::sm::config
