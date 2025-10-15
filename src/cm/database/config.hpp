/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_DATABASE_CONFIG_HPP_
#define AOS_CM_DATABASE_CONFIG_HPP_

#include <string>

namespace aos::cm::database {

/**
 * Database configuration.
 */
struct Config {
    std::string mWorkingDir;
    std::string mMigrationPath;
    std::string mMergedMigrationPath;
};

} // namespace aos::cm::database

#endif
