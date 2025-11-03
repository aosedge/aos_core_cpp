/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>

#include <Poco/JSON/Parser.h>

#include <common/utils/exception.hpp>
#include <common/utils/json.hpp>

#include "config.hpp"

namespace aos::cm::config {

/***********************************************************************************************************************
 * Constants
 **********************************************************************************************************************/

constexpr auto cDefaultServiceTTL               = "30d";
constexpr auto cDefaultLayerTTL                 = "30d";
constexpr auto cDefaultUnitStatusSendTimeout    = "30s";
constexpr auto cDefaultMaxConcurrentDownloads   = 4;
constexpr auto cDefaultRetryDelay               = "1m";
constexpr auto cDefaultMaxRetryDelay            = "30m";
constexpr auto cDefaultDownloadPartLimit        = 100;
constexpr auto cDefaultUMControllerUpdateTTL    = "30d";
constexpr auto cDefaultNodesConnectionTimeout   = "10m";
constexpr auto cDefaultSMControllerUpdateTTL    = "30d";
constexpr auto cDefaultAlertsSendPeriod         = "10s";
constexpr auto cDefaultAlertsMaxMessageSize     = 65536;
constexpr auto cDefaultAlertsMaxOfflineMessages = 25;
constexpr auto cDefaultMonitoringSendPeriod     = "1m";
constexpr auto cDefaultMigrationPath            = "/usr/share/aos/communicationmanager/migration";
constexpr auto cDefaultCertStorage              = "/var/aos/crypt/cm/";

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

namespace {

void ParseCryptConfig(const common::utils::CaseInsensitiveObjectWrapper& object, Crypt& config)
{
    config.mCACert        = object.GetValue<std::string>("caCert");
    config.mTpmDevice     = object.GetValue<std::string>("tpmDevice");
    config.mPkcs11Library = object.GetValue<std::string>("pkcs11Library");
}

void ParseUMControllerConfig(const common::utils::CaseInsensitiveObjectWrapper& object, UMController& config)
{
    config.mFileServerURL = object.GetValue<std::string>("fileServerUrl");
    config.mCMServerURL   = object.GetValue<std::string>("cmServerUrl");

    Error err = ErrorEnum::eNone;

    Tie(config.mUpdateTTL, err)
        = common::utils::ParseDuration(object.GetValue<std::string>("updateTtl", cDefaultUMControllerUpdateTTL));
    AOS_ERROR_CHECK_AND_THROW(err, "error parsing updateTtl tag");
}

void ParseMonitoringConfig(const common::utils::CaseInsensitiveObjectWrapper& object, Monitoring& config)
{
    auto err = common::config::ParseMonitoringConfig(object.GetObject("monitorConfig"), config);
    AOS_ERROR_CHECK_AND_THROW(err, "error parsing monitoring config");

    Tie(config.mSendPeriod, err)
        = common::utils::ParseDuration(object.GetValue<std::string>("sendPeriod", cDefaultMonitoringSendPeriod));
    AOS_ERROR_CHECK_AND_THROW(err, "error parsing sendPeriod tag");
}

void ParseAlertsConfig(const common::utils::CaseInsensitiveObjectWrapper& object, Alerts& config)
{
    auto err = common::config::ParseJournalAlertsConfig(object.GetObject("journalAlerts"), config.mJournalAlerts);
    AOS_ERROR_CHECK_AND_THROW(err, "error parsing journal alerts config");

    Tie(config.mSendPeriod, err)
        = common::utils::ParseDuration(object.GetValue<std::string>("sendPeriod", cDefaultAlertsSendPeriod));
    AOS_ERROR_CHECK_AND_THROW(err, "error parsing sendPeriod tag");

    config.mMaxMessageSize     = object.GetValue<int>("maxMessageSize", cDefaultAlertsMaxMessageSize);
    config.mMaxOfflineMessages = object.GetValue<int>("maxOfflineMessages", cDefaultAlertsMaxOfflineMessages);
}

void ParseDownloaderConfig(
    const common::utils::CaseInsensitiveObjectWrapper& object, const std::string& workingDir, Downloader& config)
{
    config.mDownloadDir = object.GetValue<std::string>("downloadDir", std::filesystem::path(workingDir) / "download");
    config.mMaxConcurrentDownloads = object.GetValue<int>("maxConcurrentDownloads", cDefaultMaxConcurrentDownloads);

    Error err = ErrorEnum::eNone;

    Tie(config.mRetryDelay, err)
        = common::utils::ParseDuration(object.GetValue<std::string>("retryDelay", cDefaultRetryDelay));
    AOS_ERROR_CHECK_AND_THROW(err, "error parsing retryDelay tag");

    Tie(config.mMaxRetryDelay, err)
        = common::utils::ParseDuration(object.GetValue<std::string>("maxRetryDelay", cDefaultMaxRetryDelay));
    AOS_ERROR_CHECK_AND_THROW(err, "error parsing maxRetryDelay tag");

    config.mDownloadPartLimit = object.GetValue<int>("downloadPartLimit", cDefaultDownloadPartLimit);
}

void ParseSMControllerConfig(const common::utils::CaseInsensitiveObjectWrapper& object, SMController& config)
{
    config.mFileServerURL = object.GetValue<std::string>("fileServerUrl");
    config.mCMServerURL   = object.GetValue<std::string>("cmServerUrl");

    Error err = ErrorEnum::eNone;

    Tie(config.mNodesConnectionTimeout, err) = common::utils::ParseDuration(
        object.GetValue<std::string>("nodesConnectionTimeout", cDefaultNodesConnectionTimeout));
    AOS_ERROR_CHECK_AND_THROW(err, "error parsing nodesConnectionTimeout tag");

    Tie(config.mUpdateTTL, err)
        = common::utils::ParseDuration(object.GetValue<std::string>("updateTtl", cDefaultSMControllerUpdateTTL));
    AOS_ERROR_CHECK_AND_THROW(err, "error parsing updateTtl tag");
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
        auto empty = common::utils::CaseInsensitiveObjectWrapper(Poco::makeShared<Poco::JSON::Object>());

        config.mWorkingDir = object.GetValue<std::string>("workingDir");

        ParseCryptConfig(object.Has("fcrypt") ? object.GetObject("fcrypt") : empty, config.mCrypt);
        ParseUMControllerConfig(
            object.Has("umController") ? object.GetObject("umController") : empty, config.mUMController);
        ParseMonitoringConfig(object.Has("monitoring") ? object.GetObject("monitoring") : empty, config.mMonitoring);
        ParseAlertsConfig(object.Has("alerts") ? object.GetObject("alerts") : empty, config.mAlerts);
        ParseDownloaderConfig(
            object.Has("downloader") ? object.GetObject("downloader") : empty, config.mWorkingDir, config.mDownloader);
        ParseSMControllerConfig(
            object.Has("smController") ? object.GetObject("smController") : empty, config.mSMController);

        if (auto err
            = common::config::ParseMigrationConfig(object.Has("migration") ? object.GetObject("migration") : empty,
                cDefaultMigrationPath, std::filesystem::path(config.mWorkingDir) / "migration", config.mMigration);
            err != ErrorEnum::eNone) {
            return AOS_ERROR_WRAP(err);
        }

        config.mDNSIP = object.GetValue<std::string>("dnsIp");

        config.mCertStorage = object.GetValue<std::string>("certStorage", cDefaultCertStorage);

        config.mServiceDiscoveryURL   = object.GetValue<std::string>("serviceDiscoveryUrl");
        config.mIAMProtectedServerURL = object.GetValue<std::string>("iamProtectedServerUrl");
        config.mIAMPublicServerURL    = object.GetValue<std::string>("iamPublicServerUrl");
        config.mCMServerURL           = object.GetValue<std::string>("cmServerUrl");
        config.mStorageDir
            = object.GetValue<std::string>("storageDir", std::filesystem::path(config.mWorkingDir) / "storages");
        config.mStateDir
            = object.GetValue<std::string>("stateDir", std::filesystem::path(config.mWorkingDir) / "states");
        config.mImageStoreDir
            = object.GetValue<std::string>("imageStoreDir", std::filesystem::path(config.mWorkingDir) / "imagestore");
        config.mComponentsDir
            = object.GetValue<std::string>("componentsDir", std::filesystem::path(config.mWorkingDir) / "components");
        config.mUnitConfigFile = object.GetValue<std::string>(
            "unitConfigFile", std::filesystem::path(config.mWorkingDir) / "aos_unit.cfg");

        Error err = ErrorEnum::eNone;

        Tie(config.mServiceTTL, err)
            = common::utils::ParseDuration(object.GetValue<std::string>("serviceTtlDays", cDefaultServiceTTL));
        AOS_ERROR_CHECK_AND_THROW(err, "error parsing serviceTtlDays tag");

        Tie(config.mLayerTTL, err)
            = common::utils::ParseDuration(object.GetValue<std::string>("layerTtlDays", cDefaultLayerTTL));
        AOS_ERROR_CHECK_AND_THROW(err, "error parsing layerTtlDays tag");

        Tie(config.mUnitStatusSendTimeout, err) = common::utils::ParseDuration(
            object.GetValue<std::string>("unitStatusSendTimeout", cDefaultUnitStatusSendTimeout));
        AOS_ERROR_CHECK_AND_THROW(err, "error parsing unitStatusSendTimeout tag");
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::cm::config
