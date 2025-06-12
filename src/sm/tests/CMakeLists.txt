#
# Copyright (C) 2024 EPAM Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

# ######################################################################################################################
# Common include directories
# ######################################################################################################################

include_directories(${CMAKE_SOURCE_DIR}/src)
include_directories(include)

# ######################################################################################################################
# Add tests
# ######################################################################################################################

add_subdirectory(config)
add_subdirectory(database)
add_subdirectory(image)
add_subdirectory(launcher)
add_subdirectory(monitoring)
add_subdirectory(networkmanager)
add_subdirectory(resourcemanager)
add_subdirectory(runner)
add_subdirectory(smclient)
add_subdirectory(logprovider)
add_subdirectory(alerts)
