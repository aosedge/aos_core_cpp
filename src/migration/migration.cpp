/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>

#include "migration/migration.hpp"
#include "utils/exception.hpp"

using namespace Poco::Data::Keywords;

namespace aos::common::migration {

Migration::Migration(
    Poco::Data::Session& session, const std::string& migrationDir, const std::string& mergedMigrationDir)
    : mSession(session)
    , mMergedMigrationDir(std::filesystem::absolute(mergedMigrationDir))
{
    auto absMigrationPath = std::filesystem::absolute(migrationDir).native();

    MergeMigrationFiles(absMigrationPath);

    CreateVersionTable();
}

void Migration::MigrateToVersion(int targetVersion)
{
    int currentVersion = GetCurrentVersion();

    if (currentVersion == targetVersion) {
        return;
    }

    if (currentVersion < targetVersion) {
        UpgradeDatabase(targetVersion, currentVersion);

        return;
    }

    DowngradeDatabase(targetVersion, currentVersion);
}

void Migration::ApplyMigration(const std::string& migrationScript)
{
    std::string   path = std::filesystem::path(mMergedMigrationDir) / migrationScript;
    std::ifstream file(path);

    if (!file.is_open()) {
        AOS_ERROR_THROW("Failed to open migration script: " + migrationScript, aos::ErrorEnum::eRuntime);
    }

    std::string script((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    mSession << script, now;
}

void Migration::UpgradeDatabase(int targetVersion, int currentVersion)
{
    for (int i = currentVersion + 1; i <= targetVersion; i++) {
        ApplyMigration(std::to_string(i) + "_update.up.sql");
        UpdateVersion(i);
    }
}

void Migration::DowngradeDatabase(int targetVersion, int currentVersion)
{
    for (int i = currentVersion; i > targetVersion; i--) {
        ApplyMigration(std::to_string(i) + "_update.down.sql");
        UpdateVersion(i - 1);
    }
}

void Migration::CreateVersionTable()
{
    mSession << "CREATE TABLE IF NOT EXISTS SchemaVersion (version INTEGER);", now;
    mSession << "INSERT INTO SchemaVersion (version) SELECT 0 WHERE NOT EXISTS "
                "(SELECT 1 FROM SchemaVersion)",
        now;
}

void Migration::UpdateVersion(int version)
{
    mSession << "UPDATE SchemaVersion SET version = ?;", use(version), now;
}

int Migration::GetCurrentVersion()
{
    int                   version = 0;
    Poco::Data::Statement select(mSession);

    select << "SELECT version FROM SchemaVersion LIMIT 1;", into(version), now;

    return version;
}

void Migration::MergeMigrationFiles(const std::string& migrationDir)
{
    if (!std::filesystem::exists(migrationDir)) {
        AOS_ERROR_THROW("migration path doesn't exist (" + migrationDir + ")", aos::ErrorEnum::eInvalidArgument);
    }

    std::filesystem::create_directories(mMergedMigrationDir);

    std::filesystem::copy(migrationDir, mMergedMigrationDir,
        std::filesystem::copy_options::skip_existing | std::filesystem::copy_options::recursive);
}

} // namespace aos::common::migration
