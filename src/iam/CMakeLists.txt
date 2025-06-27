#
# Copyright (C) 2024 Renesas Electronics Corporation.
# Copyright (C) 2024 EPAM Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

# ######################################################################################################################
# Dependencies
# ######################################################################################################################

find_package(Poco REQUIRED Foundation Util JSON DataSQLite)

# ######################################################################################################################
# Target aliases
# ######################################################################################################################

set(TARGET_NAMESPACE ${TARGET_NAMESPACE}::iam)
set(TARGET_PREFIX ${TARGET_PREFIX}_iam)

# ######################################################################################################################
# Modules
# ######################################################################################################################

add_subdirectory(config)
add_subdirectory(database)
add_subdirectory(identhandler)
add_subdirectory(iamclient)
add_subdirectory(iamserver)
add_subdirectory(nodeinfoprovider)
add_subdirectory(app)
