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
    Poco::Util
    aos::core::common::crypto
    aos::core::common::monitoring
    aos::core::common::pkcs11
    aos::core::common::version
    aos::core::sm::imagemanager
    aos::core::sm::launcher
    aos::core::sm::networkmanager
    aos::core::sm::nodeconfig
    aos::common::downloader
    aos::common::iamclient
    aos::common::jsonprovider
    aos::common::logger
    aos::common::network
    aos::common::ocispec
    aos::common::version
    aos::sm::alerts
    aos::sm::config
    aos::sm::database
    aos::sm::iamclient
    aos::sm::imagemanager
    aos::sm::launcher
    aos::sm::logprovider
    aos::sm::monitoring
    aos::sm::networkmanager
    aos::sm::resourcemanager
    aos::sm::smclient
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
    LINK_OPTIONS
    -rdynamic
)

# ######################################################################################################################
# Install
# ######################################################################################################################

install(TARGETS ${TARGET} RUNTIME DESTINATION bin)
