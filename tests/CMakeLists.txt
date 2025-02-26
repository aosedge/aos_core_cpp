#
# Copyright (C) 2024 Renesas Electronics Corporation.
# Copyright (C) 2024 EPAM Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

# ######################################################################################################################
# Common include directories
# ######################################################################################################################

include_directories(${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})

# ######################################################################################################################
# Add tests
# ######################################################################################################################

add_subdirectory(cmclient)
add_subdirectory(communication)
add_subdirectory(config)
add_subdirectory(filechunker)
add_subdirectory(iamclient)
add_subdirectory(imageunpacker)
