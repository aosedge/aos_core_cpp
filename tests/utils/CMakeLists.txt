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
    channel_test.cpp
    exception_test.cpp
    filesystem_test.cpp
    image_test.cpp
    json_test.cpp
    parser_test.cpp
    time_test.cpp
)

# ######################################################################################################################
# Target
# ######################################################################################################################

add_executable(${TARGET} ${SOURCES})

gtest_discover_tests(${TARGET})

# ######################################################################################################################
# Libraries
# ######################################################################################################################

target_link_libraries(${TARGET} testutils aosutils mbedtls GTest::gmock_main)
