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
    "monitoring" : {},
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
    EXPECT_STREQ(config.mCMServerURL.c_str(), "aoscm:8093");
    EXPECT_STREQ(config.mDownloadDir.c_str(), "/var/aos/servicemanager/download");
    EXPECT_STREQ(config.mExtractDir.c_str(), "/var/aos/servicemanager/extract");

    ASSERT_EQ(config.mHostBinds.size(), 3);
    EXPECT_STREQ(config.mHostBinds[0].c_str(), "dir0");
    EXPECT_STREQ(config.mHostBinds[1].c_str(), "dir1");
    EXPECT_STREQ(config.mHostBinds[2].c_str(), "dir2");

    ASSERT_EQ(config.mHosts.size(), 2);
    EXPECT_STREQ(config.mHosts[0].mHostname.c_str(), "wwwivi");
    EXPECT_STREQ(config.mHosts[0].mIP.c_str(), "127.0.0.1");
    EXPECT_STREQ(config.mHosts[1].mHostname.c_str(), "wwwaosum");
    EXPECT_STREQ(config.mHosts[1].mIP.c_str(), "0.0.0.0");

    EXPECT_STREQ(config.mIAMProtectedServerURL.c_str(), "localhost:8089");

    ASSERT_EQ(config.mJournalAlerts.mFilter.size(), 2);
    EXPECT_STREQ(config.mJournalAlerts.mFilter[0].c_str(), "test");
    EXPECT_STREQ(config.mJournalAlerts.mFilter[1].c_str(), "regexp");
    EXPECT_EQ(config.mJournalAlerts.mServiceAlertPriority, 7);
    EXPECT_EQ(config.mJournalAlerts.mSystemAlertPriority, 5);

    EXPECT_EQ(config.mServiceTTL, std::chrono::hours(24 * 10)) << config.mServiceTTL.count();
    EXPECT_EQ(config.mLayerTTL, std::chrono::hours(20)) << config.mLayerTTL.count();

    EXPECT_STREQ(config.mLayersDir.c_str(), "/var/aos/srvlib");
    EXPECT_EQ(config.mLayersPartLimit, 20);

    EXPECT_EQ(config.mLogging.mMaxPartCount, 10);
    EXPECT_EQ(config.mLogging.mMaxPartSize, 1024);

    EXPECT_STREQ(config.mMigration.mMigrationPath.c_str(), "/usr/share/aos_servicemanager/migration");
    EXPECT_STREQ(config.mMigration.mMergedMigrationPath.c_str(), "/var/aos/servicemanager/mergedMigration");

    EXPECT_EQ(config.mMonitoring.mAverageWindow, std::chrono::minutes(5)) << config.mMonitoring.mAverageWindow.count();
    EXPECT_EQ(config.mMonitoring.mPollPeriod, std::chrono::hours(1) + std::chrono::minutes(1) + std::chrono::seconds(5))
        << config.mMonitoring.mPollPeriod.count();

    EXPECT_STREQ(config.mNodeConfigFile.c_str(), "/var/aos/aos_node.cfg");
    EXPECT_EQ(config.mServiceHealthCheckTimeout, std::chrono::seconds(10)) << config.mServiceHealthCheckTimeout.count();
    EXPECT_EQ(config.mCMReconnectTimeout, std::chrono::minutes(1)) << config.mCMReconnectTimeout.count();
    EXPECT_STREQ(config.mServicesDir.c_str(), "/var/aos/servicemanager/services");
    EXPECT_EQ(config.mServicesPartLimit, 10);
    EXPECT_STREQ(config.mStateDir.c_str(), "/var/aos/state");
    EXPECT_STREQ(config.mStorageDir.c_str(), "/var/aos/storage");
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

    EXPECT_EQ(config.mServiceTTL, std::chrono::hours(24 * 30)) << config.mServiceTTL.count();
    EXPECT_EQ(config.mLayerTTL, std::chrono::hours(24 * 30)) << config.mServiceTTL.count();
    EXPECT_EQ(config.mServiceHealthCheckTimeout, std::chrono::seconds(35)) << config.mServiceHealthCheckTimeout.count();
    EXPECT_EQ(config.mCMReconnectTimeout, std::chrono::seconds(10)) << config.mCMReconnectTimeout.count();

    EXPECT_EQ(config.mMonitoring.mPollPeriod, std::chrono::seconds(35)) << config.mMonitoring.mPollPeriod.count();
    EXPECT_EQ(config.mMonitoring.mAverageWindow, std::chrono::seconds(35)) << config.mMonitoring.mAverageWindow.count();

    EXPECT_EQ(config.mCertStorage, "/var/aos/crypt/sm/");

    ASSERT_EQ(config.mWorkingDir, "test");

    EXPECT_EQ(config.mStorageDir, "test/storages");
    EXPECT_EQ(config.mLayersDir, "test/layers");
    EXPECT_EQ(config.mServicesDir, "test/services");
    EXPECT_EQ(config.mDownloadDir, "test/downloads");
    EXPECT_EQ(config.mExtractDir, "test/extracts");
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
