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
# Includes
# ######################################################################################################################

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

# ######################################################################################################################
# Target aliases
# ######################################################################################################################

set(TARGET_NAMESPACE ${TARGET_NAMESPACE}::iam)
set(TARGET_PREFIX ${TARGET_PREFIX}_iam)

set(AOS_IAM_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# ######################################################################################################################
# Modules
# ######################################################################################################################

add_subdirectory(config)
add_subdirectory(database)
add_subdirectory(fileidentifier)
add_subdirectory(iamclient)
add_subdirectory(iamserver)
add_subdirectory(nodeinfoprovider)
add_subdirectory(visidentifier)
add_subdirectory(app)

if(WITH_TEST)
    add_subdirectory(config/tests)
    add_subdirectory(database/tests)
    add_subdirectory(fileidentifier/tests)
    add_subdirectory(iamclient/tests)
    add_subdirectory(iamserver/tests)
    add_subdirectory(nodeinfoprovider/tests)
    add_subdirectory(visidentifier/tests)
endif()
