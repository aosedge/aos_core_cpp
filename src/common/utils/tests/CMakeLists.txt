#
# Copyright (C) 2024 Renesas Electronics Corporation.
# Copyright (C) 2024 EPAM Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

set(TARGET utils_test)

# ######################################################################################################################
# Sources
# ######################################################################################################################

set(SOURCES
    channel.cpp
    cleanupmanager.cpp
    exception.cpp
    filesystem.cpp
    fsplatform.cpp
    image.cpp
    json.cpp
    parser.cpp
    time.cpp
)

# ######################################################################################################################
# Target
# ######################################################################################################################

add_executable(${TARGET} ${SOURCES})

gtest_discover_tests(${TARGET})

# ######################################################################################################################
# Libraries
# ######################################################################################################################

target_link_libraries(${TARGET} aostestutils aosutils aostesttools GTest::gmock_main)
