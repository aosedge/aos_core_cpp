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
    aos::sm::config
    aos::sm::database
    aos::sm::iamclient
    aos::sm::monitoring
    aos::sm::resourcemanager
    aos::sm::networkmanager
    aos::sm::alerts
    aos::sm::smclient
    aos::sm::logprovider
    aos::common::iamclient
    aos::common::jsonprovider
    aos::common::logger
    aos::common::network
    aos::common::version
    aos::core::common::monitoring
    aos::core::common::version
    aos::core::common::crypto
    aos::core::common::pkcs11
    aos::core::sm::launcher
    aos::core::sm::nodeconfig
    aos::core::sm::networkmanager
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

# ######################################################################################################################
# Install
# ######################################################################################################################

install(TARGETS ${TARGET} RUNTIME DESTINATION bin)
