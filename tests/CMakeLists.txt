#
# Copyright (C) 2024 Renesas Electronics Corporation.
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
add_subdirectory(iamclient)
add_subdirectory(iamserver)
add_subdirectory(utils)
add_subdirectory(visidentifier)
