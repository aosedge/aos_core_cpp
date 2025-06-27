#
# Copyright (C) 2024 EPAM Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

set(TARGET_NAME networkmanager)

# ######################################################################################################################
# Sources
# ######################################################################################################################

set(SOURCES cni.cpp exec.cpp trafficmonitor.cpp)

# ######################################################################################################################
# Libraries
# ######################################################################################################################

set(LIBRARIES aos::common::network aos::common::utils aoscommon aossm Poco::JSON Poco::Foundation)

# ######################################################################################################################
# Target
# ######################################################################################################################

add_module(
    TARGET_NAME
    ${TARGET_NAME}
    LOG_MODULE
    STACK_USAGE
    ${AOS_STACK_USAGE}
    SOURCES
    ${SOURCES}
    LIBRARIES
    ${LIBRARIES}
)

if(WITH_TEST)
    add_subdirectory(tests)
endif()
