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

#include <core/common/tests/utils/log.hpp>

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
    "cmReconnectTimeout": "1m",
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
        aos::tests::utils::InitLog();

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

    EXPECT_EQ(config->mIAMProtectedServerURL, "localhost:8089");

    ASSERT_EQ(config->mJournalAlerts.mFilter.size(), 2);
    EXPECT_EQ(config->mJournalAlerts.mFilter[0], "test");
    EXPECT_EQ(config->mJournalAlerts.mFilter[1], "regexp");
    EXPECT_EQ(config->mJournalAlerts.mServiceAlertPriority, 7);
    EXPECT_EQ(config->mJournalAlerts.mSystemAlertPriority, 5);

    EXPECT_EQ(config->mLogging.mMaxPartCount, 10);
    EXPECT_EQ(config->mLogging.mMaxPartSize, 1024);

    EXPECT_EQ(config->mMigration.mMigrationPath, "/usr/share/aos_servicemanager/migration");
    EXPECT_EQ(config->mMigration.mMergedMigrationPath, "/var/aos/servicemanager/mergedMigration");

    EXPECT_EQ(config->mMonitoring.mAverageWindow, 5 * aos::Time::cMinutes);
    EXPECT_EQ(config->mMonitoring.mPollPeriod, aos::Time::cHours + aos::Time::cMinutes + 5 * aos::Time::cSeconds);

    EXPECT_EQ(config->mNodeConfigFile, "/var/aos/aos_node.cfg");
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

    EXPECT_EQ(config->mSMClientConfig.mCMReconnectTimeout, 10 * aos::Time::cSeconds);

    EXPECT_EQ(config->mMonitoring.mPollPeriod, 35 * aos::Time::cSeconds);
    EXPECT_EQ(config->mMonitoring.mAverageWindow, 35 * aos::Time::cSeconds);

    EXPECT_EQ(config->mCertStorage, "/var/aos/crypt/sm/");

    ASSERT_EQ(config->mWorkingDir, "test");

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
