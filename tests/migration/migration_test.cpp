/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024s EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>
#include <vector>

#include <Poco/Data/SQLite/Connector.h>
#include <gtest/gtest.h>

#include "migration/migration.hpp"

using namespace testing;

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

class MigrationTest : public Test {
public:
    MigrationTest() { Poco::Data::SQLite::Connector::registerConnector(); }

protected:
    void TearDown() override { std::filesystem::remove_all(mDatabasePath); }

    void SetUp() override
    {
        std::filesystem::create_directories(mMigrationDir);

        mSession = std::optional<Poco::Data::Session>(
            Poco::Data::Session("SQLite", std::filesystem::path(mDatabasePath) / "test.db"));

        mMigration.emplace(*mSession, mMigrationDir);

        CreateTestTable();
    }

    void WriteMigrationScript(const std::string& scriptName, const std::string& scriptContent)
    {
        std::ofstream file(std::filesystem::path(mMigrationDir) / scriptName);
        file << scriptContent;
    }

    std::optional<aos::common::migration::Migration> mMigration;
    std::optional<Poco::Data::Session>               mSession;
    std::string                                      mMigrationDir = "database/migration";
    std::string                                      mDatabasePath = "database";

private:
    void CreateTestTable()
    {
        *mSession << "CREATE TABLE IF NOT EXISTS test (id INTEGER PRIMARY KEY);", Poco::Data::Keywords::now;
    }
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(MigrationTest, MigrateToVersion)
{
    const char* firstVersion = "1_update.up.sql";

    WriteMigrationScript(firstVersion, "ALTER TABLE test ADD COLUMN name TEXT;");

    mMigration->MigrateToVersion(1);

    int count = 0;
    *mSession << "SELECT COUNT(*) AS CNTREC FROM pragma_table_info('test') WHERE name = 'name';",
        Poco::Data::Keywords::into(count), Poco::Data::Keywords::now;

    EXPECT_EQ(count, 1);
    EXPECT_EQ(mMigration->GetCurrentVersion(), 1);

    const char* secondVersion = "2_update.up.sql";

    WriteMigrationScript(secondVersion, "CREATE TABLE IF NOT EXISTS test2 (id INTEGER PRIMARY KEY);");

    mMigration->MigrateToVersion(2);

    count = 0;
    *mSession << "SELECT COUNT(*) AS CNTREC FROM pragma_table_info('test2');", Poco::Data::Keywords::into(count),
        Poco::Data::Keywords::now;

    EXPECT_EQ(count, 1);
    EXPECT_EQ(mMigration->GetCurrentVersion(), 2);

    const char* secondVersionDown = "2_update.down.sql";

    WriteMigrationScript(secondVersionDown, "DROP TABLE test2;");
    mMigration->MigrateToVersion(1);

    count = 0;
    *mSession << "SELECT COUNT(*) AS CNTREC FROM pragma_table_info('test2');", Poco::Data::Keywords::into(count),
        Poco::Data::Keywords::now;

    EXPECT_EQ(count, 0);
    EXPECT_EQ(mMigration->GetCurrentVersion(), 1);
}
