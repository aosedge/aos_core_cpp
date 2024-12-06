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

    void TearDown() override { std::filesystem::remove_all(cDatabasePath); }

    void SetUp() override
    {
        std::filesystem::create_directories(cMigrationDir);
        std::filesystem::create_directories(cMergedMigrationDir);

        mSession = std::optional<Poco::Data::Session>(
            Poco::Data::Session("SQLite", std::filesystem::path(cDatabasePath) / "test.db"));
    }

protected:
    static constexpr auto cMigrationDir       = "database/migration-src";
    static constexpr auto cMergedMigrationDir = "database/migration";
    static constexpr auto cDatabasePath       = "database";

    void WriteMigrationScript(const std::string& scriptName, const std::string& scriptContent,
        const std::string& dirPath = cMergedMigrationDir)
    {
        std::ofstream file(std::filesystem::path(dirPath) / scriptName);
        file << scriptContent;
    }

    void CreateTestTable()
    {
        *mSession << "CREATE TABLE IF NOT EXISTS test (id INTEGER PRIMARY KEY);", Poco::Data::Keywords::now;
    }

    std::optional<Poco::Data::Session> mSession;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(MigrationTest, MigrateToVersion)
{
    aos::common::migration::Migration migration {*mSession, cMigrationDir, cMergedMigrationDir};

    CreateTestTable();

    const char* firstVersion = "1_update.up.sql";

    WriteMigrationScript(firstVersion, "ALTER TABLE test ADD COLUMN name TEXT;");

    migration.MigrateToVersion(1);

    int count = 0;
    *mSession << "SELECT COUNT(*) AS CNTREC FROM pragma_table_info('test') WHERE name = 'name';",
        Poco::Data::Keywords::into(count), Poco::Data::Keywords::now;

    EXPECT_EQ(count, 1);
    EXPECT_EQ(migration.GetCurrentVersion(), 1);

    const char* secondVersion = "2_update.up.sql";

    WriteMigrationScript(secondVersion, "CREATE TABLE IF NOT EXISTS test2 (id INTEGER PRIMARY KEY);");

    migration.MigrateToVersion(2);

    count = 0;
    *mSession << "SELECT COUNT(*) AS CNTREC FROM pragma_table_info('test2');", Poco::Data::Keywords::into(count),
        Poco::Data::Keywords::now;

    EXPECT_EQ(count, 1);
    EXPECT_EQ(migration.GetCurrentVersion(), 2);

    const char* secondVersionDown = "2_update.down.sql";

    WriteMigrationScript(secondVersionDown, "DROP TABLE test2;");
    migration.MigrateToVersion(1);

    count = 0;
    *mSession << "SELECT COUNT(*) AS CNTREC FROM pragma_table_info('test2');", Poco::Data::Keywords::into(count),
        Poco::Data::Keywords::now;

    EXPECT_EQ(count, 0);
    EXPECT_EQ(migration.GetCurrentVersion(), 1);
}

TEST_F(MigrationTest, MergeMigration)
{
    namespace fs = std::filesystem;

    const char* firstUpSql = "1_update.up.sql";

    WriteMigrationScript(firstUpSql, "ALTER TABLE test ADD COLUMN name TEXT;", cMigrationDir);
    WriteMigrationScript(firstUpSql, "ALTER TABLE test ADD COLUMN name TEXT;", cMergedMigrationDir);

    const char* secondUpSql = "2_update.up.sql";

    WriteMigrationScript(secondUpSql, "CREATE TABLE IF NOT EXISTS test2 (id INTEGER PRIMARY KEY);", cMigrationDir);

    const char* secondDownSql = "2_update.down.sql";

    WriteMigrationScript(secondDownSql, "CREATE TABLE IF NOT EXISTS test2 (id INTEGER PRIMARY KEY);", cMigrationDir);

    aos::common::migration::Migration migration {*mSession, cMigrationDir, cMergedMigrationDir};

    EXPECT_TRUE(fs::exists(fs::path(cMergedMigrationDir) / firstUpSql));
    EXPECT_TRUE(fs::exists(fs::path(cMergedMigrationDir) / secondUpSql));
    EXPECT_TRUE(fs::exists(fs::path(cMergedMigrationDir) / secondDownSql));
}
