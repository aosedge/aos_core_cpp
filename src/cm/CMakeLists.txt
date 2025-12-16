#
# Copyright (C) 2025 EPAM Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

# ######################################################################################################################
# Dependencies
# ######################################################################################################################

find_package(
    Poco
    REQUIRED
    Crypto
    DataSQLite
    Net
    NetSSL
    Util
)

# ######################################################################################################################
# Target properties
# ######################################################################################################################

set(TARGET_NAMESPACE ${TARGET_NAMESPACE}::cm)
set(TARGET_PREFIX ${TARGET_PREFIX}_cm)

# ######################################################################################################################
# Modules
# ######################################################################################################################

add_subdirectory(app)
add_subdirectory(communication)
add_subdirectory(config)
# add_subdirectory(database)
add_subdirectory(iamclient)
add_subdirectory(networkmanager)
add_subdirectory(smcontroller)
