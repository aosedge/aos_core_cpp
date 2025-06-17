#
# Copyright (C) 2024 EPAM Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

set(TARGET_NAME app)

# ######################################################################################################################
# Sources
# ######################################################################################################################

set(SOURCES app.cpp aoscore.cpp main.cpp)

# ######################################################################################################################
# Libraries
# ######################################################################################################################

set(LIBRARIES
    aoscommon
    aossm
    aos::common::iamclient
    aos::common::jsonprovider
    aos::common::logger
    aos::common::downloader
    aos::common::ocispec
    aos::common::version
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

add_exec(
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
