#
# Copyright (C) 2024 EPAM Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

# ######################################################################################################################
# Dependencies
# ######################################################################################################################

find_package(PkgConfig REQUIRED)
find_package(Poco REQUIRED Foundation Util JSON DataSQLite)

pkg_check_modules(Systemd libsystemd REQUIRED)

# ######################################################################################################################
# Target properties
# ######################################################################################################################

set(TARGET_NAMESPACE ${TARGET_NAMESPACE}::sm)
set(TARGET_PREFIX ${TARGET_PREFIX}_sm)

# ######################################################################################################################
# Modules
# ######################################################################################################################

add_subdirectory(alerts)
add_subdirectory(app)
add_subdirectory(config)
add_subdirectory(database)
add_subdirectory(iamclient)
add_subdirectory(imagemanager)
add_subdirectory(logprovider)
add_subdirectory(monitoring)
add_subdirectory(networkmanager)
add_subdirectory(resourcemanager)
add_subdirectory(smclient)
add_subdirectory(utils)

# ######################################################################################################################
# Tests
# ######################################################################################################################

if(WITH_TEST)
    add_subdirectory(tests)
endif()
