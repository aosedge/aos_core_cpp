/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <fstream>
#include <iostream>
#include <sstream>

#include <Poco/JSON/Object.h>
#include <gtest/gtest.h>

#include <aos/test/log.hpp>

#include <sm/config/config.hpp>

using namespace testing;

/***********************************************************************************************************************
 * Constants
 **********************************************************************************************************************/

static constexpr auto cNotExistsFileName               = "not_exists.json";
static constexpr auto cInvalidConfigFileName           = "invalid.json";
static constexpr auto cConfigFileName                  = "aos_servicemanager.json";
static constexpr auto cTestDefaultValuesConfigFileName = "default_values.json";
static constexpr auto cTestServiceManagerJSON          = R"({
    "caCert": "CACert",
    "certStorage": "sm",
    "cmServerUrl": "aoscm:8093",
    "downloadDir": "/var/aos/servicemanager/download",
    "extractDir": "/var/aos/servicemanager/extract",
    "hostBinds": [
        "dir0",
        "dir1",
        "dir2"
    ],
    "hosts": [
        {
            "hostName": "wwwivi",
            "ip": "127.0.0.1"
        },
        {
            "hostName": "wwwaosum",
            "ip": "0.0.0.0"
        }
    ],
    "iamProtectedServerUrl": "localhost:8089",
    "iamPublicServerUrl": "localhost:8090",
    "journalAlerts": {
        "filter": [
            "test",
            "regexp"
        ],
        "serviceAlertPriority": 7,
        "systemAlertPriority": 5
    },
    "serviceTtl": "10d",
    "cmReconnectTimeout": "1m",
    "layerTtl": "20h",
    "layersDir": "/var/aos/srvlib",
    "layersPartLimit": 20,
    "logging": {
        "maxPartCount": 10,
        "maxPartSize": 1024
    },
    "migration": {
        "mergedMigrationPath": "/var/aos/servicemanager/mergedMigration",
        "migrationPath": "/usr/share/aos_servicemanager/migration"
    },
    "monitoring": {
        "averageWindow": "5m",
        "pollPeriod": "1h1m5s"
    },
    "nodeConfigFile": "/var/aos/aos_node.cfg",
    "serviceHealthCheckTimeout": "10s",
    "servicesDir": "/var/aos/servicemanager/services",
    "servicesPartLimit": 10,
    "stateDir": "/var/aos/state",
    "storageDir": "/var/aos/storage",
    "workingDir": "workingDir",
    "removeOutdatedPeriod": "1d"
})";
static constexpr auto cTestDefaultValuesJSON           = R"({
    "workingDir": "test",
    "journalAlerts": {
        "filter": [
            "test",
            "regexp"
        ],
        "serviceAlertPriority": 999,
        "systemAlertPriority": 999
    }
})";
static constexpr auto cInvalidJSON                     = R"({"invalid json" : {,})";
static constexpr auto cDefaultServiceAlertPriority     = 4;
static constexpr auto cDefaultSystemAlertPriority      = 3;

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

class ConfigTest : public Test {
public:
    void SetUp() override
    {
        aos::test::InitLog();

        if (std::ofstream file(cConfigFileName); file.good()) {
            file << cTestServiceManagerJSON;
        }

        if (std::ofstream file(cTestDefaultValuesConfigFileName); file.good()) {
            file << cTestDefaultValuesJSON;
        }

        if (std::ofstream file(cInvalidConfigFileName); file.good()) {
            file << cInvalidJSON;
        }

        std::remove(cNotExistsFileName);
    }

    void TearDown() override
    {
        std::remove(cConfigFileName);
        std::remove(cTestDefaultValuesConfigFileName);
        std::remove(cInvalidConfigFileName);
    }
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(ConfigTest, ParseConfig)
{
    auto config = std::make_unique<aos::sm::config::Config>();

    ASSERT_TRUE(aos::sm::config::ParseConfig(cConfigFileName, *config).IsNone());

    EXPECT_EQ(config->mIAMClientConfig.mCACert, "CACert");
    EXPECT_EQ(config->mIAMClientConfig.mIAMPublicServerURL, "localhost:8090");

    EXPECT_EQ(config->mCertStorage, "sm");

    EXPECT_EQ(config->mSMClientConfig.mCertStorage, "sm");
    EXPECT_EQ(config->mSMClientConfig.mCMServerURL, "aoscm:8093");
    EXPECT_EQ(config->mSMClientConfig.mCMReconnectTimeout, aos::Time::cMinutes);

    EXPECT_EQ(config->mLauncherConfig.mWorkDir, "workingDir");
    EXPECT_EQ(config->mLauncherConfig.mStorageDir, "/var/aos/storage");
    EXPECT_EQ(config->mLauncherConfig.mStateDir, "/var/aos/state");

    ASSERT_EQ(config->mLauncherConfig.mHostBinds.Size(), 3);
    EXPECT_EQ(config->mLauncherConfig.mHostBinds[0], "dir0");
    EXPECT_EQ(config->mLauncherConfig.mHostBinds[1], "dir1");
    EXPECT_EQ(config->mLauncherConfig.mHostBinds[2], "dir2");

    ASSERT_EQ(config->mLauncherConfig.mHosts.Size(), 2);
    EXPECT_EQ(config->mLauncherConfig.mHosts[0].mHostname, "wwwivi");
    EXPECT_EQ(config->mLauncherConfig.mHosts[0].mIP, "127.0.0.1");
    EXPECT_EQ(config->mLauncherConfig.mHosts[1].mHostname, "wwwaosum");
    EXPECT_EQ(config->mLauncherConfig.mHosts[1].mIP, "0.0.0.0");

    EXPECT_EQ(config->mLauncherConfig.mRemoveOutdatedPeriod, 24 * aos::Time::cHours);

    EXPECT_EQ(config->mIAMProtectedServerURL, "localhost:8089");

    ASSERT_EQ(config->mJournalAlerts.mFilter.size(), 2);
    EXPECT_EQ(config->mJournalAlerts.mFilter[0], "test");
    EXPECT_EQ(config->mJournalAlerts.mFilter[1], "regexp");
    EXPECT_EQ(config->mJournalAlerts.mServiceAlertPriority, 7);
    EXPECT_EQ(config->mJournalAlerts.mSystemAlertPriority, 5);

    EXPECT_EQ(config->mServiceManagerConfig.mTTL, aos::Time::cHours * 24 * 10);
    EXPECT_EQ(config->mServiceManagerConfig.mDownloadDir, "/var/aos/servicemanager/download");
    EXPECT_EQ(config->mServiceManagerConfig.mServicesDir, "/var/aos/servicemanager/services");
    EXPECT_EQ(config->mServiceManagerConfig.mRemoveOutdatedPeriod, 24 * aos::Time::cHours);

    EXPECT_EQ(config->mLayerManagerConfig.mTTL, aos::Time::cHours * 20);
    EXPECT_EQ(config->mLayerManagerConfig.mDownloadDir, "/var/aos/servicemanager/download");
    EXPECT_EQ(config->mLayerManagerConfig.mLayersDir, "/var/aos/srvlib");
    EXPECT_EQ(config->mLayerManagerConfig.mRemoveOutdatedPeriod, 24 * aos::Time::cHours);

    EXPECT_EQ(config->mLayersPartLimit, 20);

    EXPECT_EQ(config->mLogging.mMaxPartCount, 10);
    EXPECT_EQ(config->mLogging.mMaxPartSize, 1024);

    EXPECT_EQ(config->mMigration.mMigrationPath, "/usr/share/aos_servicemanager/migration");
    EXPECT_EQ(config->mMigration.mMergedMigrationPath, "/var/aos/servicemanager/mergedMigration");

    EXPECT_EQ(config->mMonitoring.mAverageWindow, 5 * aos::Time::cMinutes);
    EXPECT_EQ(config->mMonitoring.mPollPeriod, aos::Time::cHours + aos::Time::cMinutes + 5 * aos::Time::cSeconds);

    EXPECT_EQ(config->mNodeConfigFile, "/var/aos/aos_node.cfg");
    EXPECT_EQ(config->mServicesPartLimit, 10);
    EXPECT_EQ(config->mWorkingDir, "workingDir");
}

TEST_F(ConfigTest, DefaultValuesAreUsed)
{
    auto config = std::make_unique<aos::sm::config::Config>();

    ASSERT_TRUE(aos::sm::config::ParseConfig(cTestDefaultValuesConfigFileName, *config).IsNone());

    ASSERT_EQ(config->mJournalAlerts.mFilter.size(), 2);
    EXPECT_EQ(config->mJournalAlerts.mFilter[0], "test");
    EXPECT_EQ(config->mJournalAlerts.mFilter[1], "regexp");

    EXPECT_EQ(config->mJournalAlerts.mServiceAlertPriority, cDefaultServiceAlertPriority);
    EXPECT_EQ(config->mJournalAlerts.mSystemAlertPriority, cDefaultSystemAlertPriority);

    EXPECT_EQ(config->mServiceManagerConfig.mTTL, aos::Time::cHours * 24 * 30);
    EXPECT_EQ(config->mLayerManagerConfig.mTTL, aos::Time::cHours * 24 * 30);
    EXPECT_EQ(config->mSMClientConfig.mCMReconnectTimeout, 10 * aos::Time::cSeconds);

    EXPECT_EQ(config->mMonitoring.mPollPeriod, 35 * aos::Time::cSeconds);
    EXPECT_EQ(config->mMonitoring.mAverageWindow, 35 * aos::Time::cSeconds);

    EXPECT_EQ(config->mCertStorage, "/var/aos/crypt/sm/");

    ASSERT_EQ(config->mWorkingDir, "test");

    EXPECT_EQ(config->mLauncherConfig.mStorageDir, "test/storages");
    EXPECT_EQ(config->mLauncherConfig.mStateDir, "test/states");

    EXPECT_EQ(config->mLayerManagerConfig.mLayersDir, "test/layers");
    EXPECT_EQ(config->mServiceManagerConfig.mServicesDir, "test/services");
    EXPECT_EQ(config->mServiceManagerConfig.mDownloadDir, "test/downloads");
    EXPECT_EQ(config->mNodeConfigFile, "test/aos_node.cfg");
}

TEST_F(ConfigTest, ErrorReturnedOnFileMissing)
{
    auto config = std::make_unique<aos::sm::config::Config>();

    ASSERT_EQ(aos::sm::config::ParseConfig(cNotExistsFileName, *config), aos::ErrorEnum::eNotFound)
        << "not found error expected";
}

TEST_F(ConfigTest, ErrorReturnedOnInvalidJSONData)
{
    auto config = std::make_unique<aos::sm::config::Config>();

    ASSERT_EQ(aos::sm::config::ParseConfig(cInvalidConfigFileName, *config), aos::ErrorEnum::eFailed)
        << "failed error expected";
}
