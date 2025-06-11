#
# Copyright (C) 2024 EPAM Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

set(TARGET_NAME logprovider)

# ######################################################################################################################
# Sources
# ######################################################################################################################

set(SOURCES logprovider.cpp)

# ######################################################################################################################
# Libraries
# ######################################################################################################################

set(LIBRARIES aoscommon ${TARGET_NAMESPACE}::utils ${COMMON_NAMESPACE}::logprovider Poco::JSON Poco::Foundation)

# ######################################################################################################################
# Target
# ######################################################################################################################

set(LOG_MODULE_NAME logprovider)

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
