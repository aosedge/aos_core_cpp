#
# Copyright (C) 2024 Renesas Electronics Corporation.
# Copyright (C) 2024 EPAM Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

set(TARGET_NAME utils_test)

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

set(TARGET ${TARGET_PREFIX}_${TARGET_NAME})

add_executable(${TARGET} ${SOURCES})

gtest_discover_tests(${TARGET})

# ######################################################################################################################
# Libraries
# ######################################################################################################################

target_link_libraries(${TARGET} ${TARGET_NAMESPACE}::utils ${TARGET_NAMESPACE}::tests::utils GTest::gmock_main)
