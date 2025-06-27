/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_MIGRATION_MIGRATION_HPP_
#define AOS_COMMON_MIGRATION_MIGRATION_HPP_

#include <filesystem>

#include <Poco/Data/Session.h>

namespace aos::common::migration {

/**
 * Database migrator.
 */
class Migration {
public:
    /**
     * Creates database migrator instance.
     *
     * @param[out] session database session.
     * @param migrationDir directory with migration scripts.
     * @param mergedMigrationDir directory with merged migration scripts.
     */
    Migration(Poco::Data::Session& session, const std::string& migrationDir, const std::string& mergedMigrationDir);

    /**
     * Migrates database to the specified version.
     *
     * @param targetVersion target database version.
     */
    void MigrateToVersion(int targetVersion);

    /**
     * Returns current database version.
     *
     * @return current database version.
     */
    int GetCurrentVersion();

private:
    void ApplyMigration(const std::string& migrationScript);
    void CreateVersionTable();
    void UpdateVersion(int version);
    void UpgradeDatabase(int targetVersion, int currentVersion);
    void DowngradeDatabase(int targetVersion, int currentVersion);

    void MergeMigrationFiles(const std::string& migrationDir);

    Poco::Data::Session&  mSession;
    std::filesystem::path mMergedMigrationDir;
};

} // namespace aos::common::migration

#endif
