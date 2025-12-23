/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include <Poco/JSON/Object.h>

#include <gtest/gtest.h>

#include <common/config/config.hpp>
#include <common/utils/utils.hpp>

using namespace testing;

namespace {

/***********************************************************************************************************************
 * Constants
 **********************************************************************************************************************/

constexpr auto cTestMonitoringJSON = R"({
    "monitoring": {
        "pollPeriod": "1m",
        "averageWindow": "5m"
    }
})";

constexpr auto cTestMigrationJSON = R"({
    "migration": {
        "migrationPath": "/custom/migration/path",
        "mergedMigrationPath": "/custom/merged/path"
    }
})";

constexpr auto cTestJournalAlertsJSON = R"({
    "journalAlerts": {
        "filter": ["test1", "test2", "test3"],
        "serviceAlertPriority": 6,
        "systemAlertPriority": 2
    }
})";

} // namespace

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

class CommonConfigTest : public Test {
public:
    void SetUp() override
    {
        if (std::ofstream file("test_monitoring.json"); file.good()) {
            file << cTestMonitoringJSON;
        }

        if (std::ofstream file("test_migration.json"); file.good()) {
            file << cTestMigrationJSON;
        }

        if (std::ofstream file("test_journal_alerts.json"); file.good()) {
            file << cTestJournalAlertsJSON;
        }
    }

    void TearDown() override
    {
        std::remove("test_monitoring.json");
        std::remove("test_migration.json");
        std::remove("test_journal_alerts.json");
    }
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(CommonConfigTest, ParseMonitoringConfig)
{
    std::ifstream file("test_monitoring.json");
    ASSERT_TRUE(file.is_open());

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    auto jsonObject = aos::common::utils::ParseJson(content);
    ASSERT_TRUE(jsonObject.mError.IsNone());

    Poco::JSON::Object::Ptr                          object = jsonObject.mValue.extract<Poco::JSON::Object::Ptr>();
    aos::common::utils::CaseInsensitiveObjectWrapper wrapper(object);

    aos::monitoring::Config config;

    EXPECT_NO_THROW(aos::common::config::ParseMonitoringConfig(wrapper.GetObject("monitoring"), config));

    EXPECT_EQ(config.mPollPeriod, aos::Time::cMinutes);
    EXPECT_EQ(config.mAverageWindow, aos::Time::cMinutes * 5);
}

TEST_F(CommonConfigTest, ParseMigrationConfig)
{
    std::ifstream file("test_migration.json");
    ASSERT_TRUE(file.is_open());

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    auto jsonObject = aos::common::utils::ParseJson(content);
    ASSERT_TRUE(jsonObject.mError.IsNone());

    Poco::JSON::Object::Ptr                          object = jsonObject.mValue.extract<Poco::JSON::Object::Ptr>();
    aos::common::utils::CaseInsensitiveObjectWrapper wrapper(object);

    aos::common::config::Migration config;

    EXPECT_NO_THROW(aos::common::config::ParseMigrationConfig(
        wrapper.GetObject("migration"), "/default/migration/path", "/default/merged/path", config));

    EXPECT_EQ(config.mMigrationPath, "/custom/migration/path");
    EXPECT_EQ(config.mMergedMigrationPath, "/custom/merged/path");
}

TEST_F(CommonConfigTest, ParseJournalAlertsConfig)
{
    std::ifstream file("test_journal_alerts.json");
    ASSERT_TRUE(file.is_open());

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    auto jsonObject = aos::common::utils::ParseJson(content);
    ASSERT_TRUE(jsonObject.mError.IsNone());

    Poco::JSON::Object::Ptr                          object = jsonObject.mValue.extract<Poco::JSON::Object::Ptr>();
    aos::common::utils::CaseInsensitiveObjectWrapper wrapper(object);

    aos::common::config::JournalAlerts config;

    EXPECT_NO_THROW(aos::common::config::ParseJournalAlertsConfig(wrapper.GetObject("journalAlerts"), config));

    std::vector<std::string> expectedFilter = {"test1", "test2", "test3"};
    EXPECT_EQ(config.mFilter, expectedFilter);
    EXPECT_EQ(config.mServiceAlertPriority, 6);
    EXPECT_EQ(config.mSystemAlertPriority, 2);
}
