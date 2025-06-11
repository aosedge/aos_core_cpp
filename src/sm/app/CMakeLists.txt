#
# Copyright (C) 2024 EPAM Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

set(TARGET_NAME app)

# ######################################################################################################################
# Sources
# ######################################################################################################################

set(SOURCES app.cpp aoscore.cpp)

# ######################################################################################################################
# Libraries
# ######################################################################################################################

set(LIBRARIES
    aoscommon
    aossm
    ${COMMON_NAMESPACE}::iamclient
    ${COMMON_NAMESPACE}::jsonprovider
    ${COMMON_NAMESPACE}::logger
    ${COMMON_NAMESPACE}::downloader
    ${COMMON_NAMESPACE}::ocispec
    ${TARGET_NAMESPACE}::config
    ${TARGET_NAMESPACE}::database
    ${TARGET_NAMESPACE}::image
    ${TARGET_NAMESPACE}::launcher
    ${TARGET_NAMESPACE}::monitoring
    ${TARGET_NAMESPACE}::resourcemanager
    ${TARGET_NAMESPACE}::runner
    ${TARGET_NAMESPACE}::networkmanager
    ${TARGET_NAMESPACE}::alerts
    ${TARGET_NAMESPACE}::smclient
    ${TARGET_NAMESPACE}::logprovider
    Poco::Util
)

# ######################################################################################################################
# Target
# ######################################################################################################################

set(LOG_MODULE_NAME app)

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
