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

#include "config/config.hpp"

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
    "workingDir": "workingDir"
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
    auto [config, error] = aos::sm::config::ParseConfig(cConfigFileName);

    ASSERT_EQ(error, aos::ErrorEnum::eNone);

    EXPECT_STREQ(config.mIAMClientConfig.mCACert.c_str(), "CACert");
    EXPECT_STREQ(config.mIAMClientConfig.mIAMPublicServerURL.c_str(), "localhost:8090");

    EXPECT_STREQ(config.mCertStorage.c_str(), "sm");

    EXPECT_STREQ(config.mSMClientConfig.mCertStorage.c_str(), "sm");
    EXPECT_STREQ(config.mSMClientConfig.mCMServerURL.c_str(), "aoscm:8093");
    EXPECT_EQ(config.mSMClientConfig.mCMReconnectTimeout, std::chrono::minutes(1))
        << config.mSMClientConfig.mCMReconnectTimeout.count();

    EXPECT_STREQ(config.mLauncherConfig.mWorkDir.CStr(), "workingDir");
    EXPECT_STREQ(config.mLauncherConfig.mStorageDir.CStr(), "/var/aos/storage");
    EXPECT_STREQ(config.mLauncherConfig.mStateDir.CStr(), "/var/aos/state");

    ASSERT_EQ(config.mLauncherConfig.mHostBinds.Size(), 3);
    EXPECT_STREQ(config.mLauncherConfig.mHostBinds[0].CStr(), "dir0");
    EXPECT_STREQ(config.mLauncherConfig.mHostBinds[1].CStr(), "dir1");
    EXPECT_STREQ(config.mLauncherConfig.mHostBinds[2].CStr(), "dir2");

    ASSERT_EQ(config.mLauncherConfig.mHosts.Size(), 2);
    EXPECT_STREQ(config.mLauncherConfig.mHosts[0].mHostname.CStr(), "wwwivi");
    EXPECT_STREQ(config.mLauncherConfig.mHosts[0].mIP.CStr(), "127.0.0.1");
    EXPECT_STREQ(config.mLauncherConfig.mHosts[1].mHostname.CStr(), "wwwaosum");
    EXPECT_STREQ(config.mLauncherConfig.mHosts[1].mIP.CStr(), "0.0.0.0");

    EXPECT_STREQ(config.mIAMProtectedServerURL.c_str(), "localhost:8089");

    ASSERT_EQ(config.mJournalAlerts.mFilter.size(), 2);
    EXPECT_STREQ(config.mJournalAlerts.mFilter[0].c_str(), "test");
    EXPECT_STREQ(config.mJournalAlerts.mFilter[1].c_str(), "regexp");
    EXPECT_EQ(config.mJournalAlerts.mServiceAlertPriority, 7);
    EXPECT_EQ(config.mJournalAlerts.mSystemAlertPriority, 5);

    EXPECT_EQ(config.mServiceManagerConfig.mTTL, aos::Time::cHours * 24 * 10);
    EXPECT_STREQ(config.mServiceManagerConfig.mDownloadDir.CStr(), "/var/aos/servicemanager/download");
    EXPECT_STREQ(config.mServiceManagerConfig.mServicesDir.CStr(), "/var/aos/servicemanager/services");

    EXPECT_EQ(config.mLayerManagerConfig.mTTL, aos::Time::cHours * 20);
    EXPECT_STREQ(config.mLayerManagerConfig.mDownloadDir.CStr(), "/var/aos/servicemanager/download");
    EXPECT_STREQ(config.mLayerManagerConfig.mLayersDir.CStr(), "/var/aos/srvlib");

    EXPECT_EQ(config.mLayersPartLimit, 20);

    EXPECT_EQ(config.mLogging.mMaxPartCount, 10);
    EXPECT_EQ(config.mLogging.mMaxPartSize, 1024);

    EXPECT_STREQ(config.mMigration.mMigrationPath.c_str(), "/usr/share/aos_servicemanager/migration");
    EXPECT_STREQ(config.mMigration.mMergedMigrationPath.c_str(), "/var/aos/servicemanager/mergedMigration");

    EXPECT_EQ(config.mMonitoring.mAverageWindow, 5 * aos::Time::cMinutes);
    EXPECT_EQ(config.mMonitoring.mPollPeriod, aos::Time::cHours + aos::Time::cMinutes + 5 * aos::Time::cSeconds);

    EXPECT_STREQ(config.mNodeConfigFile.c_str(), "/var/aos/aos_node.cfg");
    EXPECT_EQ(config.mServicesPartLimit, 10);
    EXPECT_STREQ(config.mWorkingDir.c_str(), "workingDir");
}

TEST_F(ConfigTest, DefaultValuesAreUsed)
{
    auto [config, error] = aos::sm::config::ParseConfig(cTestDefaultValuesConfigFileName);

    ASSERT_EQ(error, aos::ErrorEnum::eNone);

    ASSERT_EQ(config.mJournalAlerts.mFilter.size(), 2);
    EXPECT_STREQ(config.mJournalAlerts.mFilter[0].c_str(), "test");
    EXPECT_STREQ(config.mJournalAlerts.mFilter[1].c_str(), "regexp");

    EXPECT_EQ(config.mJournalAlerts.mServiceAlertPriority, cDefaultServiceAlertPriority);
    EXPECT_EQ(config.mJournalAlerts.mSystemAlertPriority, cDefaultSystemAlertPriority);

    EXPECT_EQ(config.mServiceManagerConfig.mTTL, aos::Time::cHours * 24 * 30);
    EXPECT_EQ(config.mLayerManagerConfig.mTTL, aos::Time::cHours * 24 * 30);
    EXPECT_EQ(config.mSMClientConfig.mCMReconnectTimeout, std::chrono::seconds(10))
        << config.mSMClientConfig.mCMReconnectTimeout.count();

    EXPECT_EQ(config.mMonitoring.mPollPeriod, 35 * aos::Time::cSeconds);
    EXPECT_EQ(config.mMonitoring.mAverageWindow, 35 * aos::Time::cSeconds);

    EXPECT_EQ(config.mCertStorage, "/var/aos/crypt/sm/");

    ASSERT_EQ(config.mWorkingDir, "test");

    EXPECT_STREQ(config.mLauncherConfig.mStorageDir.CStr(), "test/storages");
    EXPECT_STREQ(config.mLauncherConfig.mStateDir.CStr(), "test/states");

    EXPECT_EQ(config.mLayerManagerConfig.mLayersDir, "test/layers");
    EXPECT_STREQ(config.mServiceManagerConfig.mServicesDir.CStr(), "test/services");
    EXPECT_STREQ(config.mServiceManagerConfig.mDownloadDir.CStr(), "test/downloads");
    EXPECT_EQ(config.mNodeConfigFile, "test/aos_node.cfg");
}

TEST_F(ConfigTest, ErrorReturnedOnFileMissing)
{
    auto [config, error] = aos::sm::config::ParseConfig(cNotExistsFileName);

    ASSERT_EQ(error, aos::ErrorEnum::eNotFound) << "not found error expected";
}

TEST_F(ConfigTest, ErrorReturnedOnInvalidJSONData)
{
    auto [config, error] = aos::sm::config::ParseConfig(cInvalidConfigFileName);

    ASSERT_EQ(error, aos::ErrorEnum::eFailed) << "failed error expected";
}
