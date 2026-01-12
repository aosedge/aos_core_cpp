/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>

#include <Poco/JSON/Parser.h>

#include <core/common/tools/logger.hpp>

#include <common/utils/exception.hpp>
#include <common/utils/json.hpp>

#include "config.hpp"

namespace aos::cm::config {

/***********************************************************************************************************************
 * Constants
 **********************************************************************************************************************/

constexpr auto cDefaultAlertsSendPeriod               = "10s";
constexpr auto cDefaultCloudResponseWaitTimeout       = "10s";
constexpr auto cDefaultLauncherInstanceTTL            = "30d";
constexpr auto cDefaultLauncherNodesConnectionTimeout = "10m";
constexpr auto cDefaultMonitoringSendPeriod           = "1m";
constexpr auto cDefaultSMConnectionTimeout            = "1m";
constexpr auto cDefaultUnitStatusSendTimeout          = "30s";
constexpr auto cDefaultUpdateItemTTL                  = "30d";
constexpr auto cDefaultRemoveOutdatedPeriod           = "24h";
constexpr auto cDefaultMigrationPath                  = "/usr/share/aos/communicationmanager/migration";
constexpr auto cDefaultCertStorage                    = "/var/aos/crypt/cm/";
constexpr auto cDefaultDNSStoragePath                 = "/var/aos/dns";

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

namespace {

void ParseMonitoringConfig(const common::utils::CaseInsensitiveObjectWrapper& object, Monitoring& config)
{
    common::config::ParseMonitoringConfig(object, config);

    Error err;

    Tie(config.mSendPeriod, err)
        = common::utils::ParseDuration(object.GetValue<std::string>("sendPeriod", cDefaultMonitoringSendPeriod));
    AOS_ERROR_CHECK_AND_THROW(err, "error parsing sendPeriod tag");
}

void ParseNodeInfoProviderConfig(
    const common::utils::CaseInsensitiveObjectWrapper& object, nodeinfoprovider::Config& config)
{
    Error err;

    Tie(config.mSMConnectionTimeout, err) = common::utils::ParseDuration(
        object.GetValue<std::string>("smConnectionTimeout", cDefaultSMConnectionTimeout));
    AOS_ERROR_CHECK_AND_THROW(err, "error parsing smConnectionTimeout tag");
}

void ParseAlertsConfig(const common::utils::CaseInsensitiveObjectWrapper& object, alerts::Config& config)
{
    Error err;

    Tie(config.mSendPeriod, err)
        = common::utils::ParseDuration(object.GetValue<std::string>("sendPeriod", cDefaultAlertsSendPeriod));
    AOS_ERROR_CHECK_AND_THROW(err, "error parsing sendPeriod tag");
}

void ParseImageManagerConfig(const common::utils::CaseInsensitiveObjectWrapper& object, const std::string& workingDir,
    imagemanager::Config& config)
{
    auto err = config.mInstallPath.Assign(
        object.GetValue<std::string>("installPath", std::filesystem::path(workingDir) / "install").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "error parsing installPath tag");

    err = config.mDownloadPath.Assign(
        object.GetValue<std::string>("downloadPath", std::filesystem::path(workingDir) / "download").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "error parsing downloadPath tag");

    Tie(config.mUpdateItemTTL, err)
        = common::utils::ParseDuration(object.GetValue<std::string>("updateItemTtl", cDefaultUpdateItemTTL));
    AOS_ERROR_CHECK_AND_THROW(err, "error parsing updateItemTtl tag");

    Tie(config.mRemoveOutdatedPeriod, err) = common::utils::ParseDuration(
        object.GetValue<std::string>("removeOutdatedPeriod", cDefaultRemoveOutdatedPeriod));
    AOS_ERROR_CHECK_AND_THROW(err, "error parsing removeOutdatedPeriod tag");
}

void ParseLauncherConfig(const common::utils::CaseInsensitiveObjectWrapper& object, launcher::Config& config)
{
    Error err;

    Tie(config.mNodesConnectionTimeout, err) = common::utils::ParseDuration(
        object.GetValue<std::string>("nodesConnectionTimeout", cDefaultLauncherNodesConnectionTimeout));
    AOS_ERROR_CHECK_AND_THROW(err, "error parsing nodesConnectionTimeout tag");

    Tie(config.mInstanceTTL, err)
        = common::utils::ParseDuration(object.GetValue<std::string>("instanceTtl", cDefaultLauncherInstanceTTL));
    AOS_ERROR_CHECK_AND_THROW(err, "error parsing instanceTtl tag");
}

} // namespace

/***********************************************************************************************************************
 * Public functions
 **********************************************************************************************************************/

Error ParseConfig(const std::string& filename, Config& config)
{
    LOG_DBG() << "Parsing config file" << Log::Field("file", filename.c_str());

    std::ifstream file(filename);

    if (!file.is_open()) {
        return ErrorEnum::eNotFound;
    }

    try {
        Poco::JSON::Parser                          parser;
        auto                                        result = parser.parse(file);
        common::utils::CaseInsensitiveObjectWrapper object(result);
        auto empty = common::utils::CaseInsensitiveObjectWrapper(Poco::makeShared<Poco::JSON::Object>());

        config.mCACert     = object.GetValue<std::string>("caCert");
        config.mWorkingDir = object.GetValue<std::string>("workingDir");

        ParseMonitoringConfig(object.Has("monitoring") ? object.GetObject("monitoring") : empty, config.mMonitoring);
        ParseNodeInfoProviderConfig(
            object.Has("nodeInfoProvider") ? object.GetObject("nodeInfoProvider") : empty, config.mNodeInfoProvider);
        ParseAlertsConfig(object.Has("alerts") ? object.GetObject("alerts") : empty, config.mAlerts);
        ParseImageManagerConfig(object.Has("imageManager") ? object.GetObject("imageManager") : empty,
            config.mWorkingDir, config.mImageManager);
        ParseLauncherConfig(object.Has("launcher") ? object.GetObject("launcher") : empty, config.mLauncher);

        common::config::ParseMigrationConfig(object.Has("migration") ? object.GetObject("migration") : empty,
            cDefaultMigrationPath, std::filesystem::path(config.mWorkingDir) / "migration", config.mMigration);

        config.mDNSStoragePath = object.GetValue<std::string>("dnsStoragePath", cDefaultDNSStoragePath);
        config.mDNSIP          = object.GetValue<std::string>("dnsIp");

        config.mCertStorage = object.GetValue<std::string>("certStorage", cDefaultCertStorage);

        config.mServiceDiscoveryURL         = object.GetValue<std::string>("serviceDiscoveryUrl");
        config.mOverrideServiceDiscoveryURL = object.GetValue<std::string>("overrideServiceDiscoveryUrl", "");
        config.mIAMProtectedServerURL       = object.GetValue<std::string>("iamProtectedServerUrl");
        config.mIAMPublicServerURL          = object.GetValue<std::string>("iamPublicServerUrl");
        config.mFileServerURL               = object.GetValue<std::string>("fileServerUrl");
        config.mCMServerURL                 = object.GetValue<std::string>("cmServerUrl");
        config.mStorageDir
            = object.GetValue<std::string>("storageDir", std::filesystem::path(config.mWorkingDir) / "storages");
        config.mStateDir
            = object.GetValue<std::string>("stateDir", std::filesystem::path(config.mWorkingDir) / "states");
        config.mUnitConfigFile = object.GetValue<std::string>(
            "unitConfigFile", std::filesystem::path(config.mWorkingDir) / "aos_unit.cfg");

        Error err = ErrorEnum::eNone;

        Tie(config.mUnitStatusSendTimeout, err) = common::utils::ParseDuration(
            object.GetValue<std::string>("unitStatusSendTimeout", cDefaultUnitStatusSendTimeout));
        AOS_ERROR_CHECK_AND_THROW(err, "error parsing unitStatusSendTimeout tag");

        Tie(config.mCloudResponseWaitTimeout, err) = common::utils::ParseDuration(
            object.GetValue<std::string>("cloudResponseWaitTimeout", cDefaultCloudResponseWaitTimeout));
        AOS_ERROR_CHECK_AND_THROW(err, "error parsing cloudResponseWaitTimeout tag");
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::cm::config
