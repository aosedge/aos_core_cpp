#
# Copyright (C) 2024 EPAM Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

set(TARGET_NAME smclient_test)

# ######################################################################################################################
# Sources
# ######################################################################################################################

set(SOURCES smclient_test.cpp)

# ######################################################################################################################
# Libraries
# ######################################################################################################################

# target_link_libraries(
#     ${TARGET}
#     smclient
#     aoscommon
#     aosiamclient
#     aostestcore
#     aostestcommon
#     GTest::gmock_main
# )

set(LIBRARIES ${TARGET_NAMESPACE}::smclient aostestcore ${COMMON_TESTS_NAMESPACE}::utils GTest::gmock_main)

# ######################################################################################################################
# Target
# ######################################################################################################################

add_test(
    TARGET_NAME
    ${TARGET_NAME}
    LOG_MODULE
    SOURCES
    ${SOURCES}
    LIBRARIES
    ${LIBRARIES}
)
