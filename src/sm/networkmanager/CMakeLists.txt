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

set(LIBRARIES aoscommon aossm ${COMMON_NAMESPACE}::network ${COMMON_NAMESPACE}::utils Poco::JSON Poco::Foundation)

# ######################################################################################################################
# Target
# ######################################################################################################################

set(LOG_MODULE_NAME networkmanager)

add_module(
    TARGET_NAME
    ${TARGET_NAME}
    LOG_MODULE
    ${LOG_MODULE_NAME}
    STACK_USAGE
    ${AOS_STACK_USAGE}
    SOURCES
    ${SOURCES}
    LIBRARIES
    ${LIBRARIES}
)
