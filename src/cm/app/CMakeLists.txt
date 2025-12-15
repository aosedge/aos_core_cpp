#
# Copyright (C) 2024 EPAM Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

# ######################################################################################################################
# Target name
# ######################################################################################################################

set(TARGET_NAME app)

# ######################################################################################################################
# Sources
# ######################################################################################################################

set(SOURCES aoscore.cpp app.cpp main.cpp)

# ######################################################################################################################
# Libraries
# ######################################################################################################################

set(LIBRARIES
    aos::cm::communication
    aos::cm::config
    aos::cm::database
    aos::cm::iamclient
    aos::cm::networkmanager
    aos::cm::smcontroller
    aos::cm::unitconfig
    aos::common::downloader
    aos::common::fileserver
    aos::common::logger
    aos::common::ocispec
    aos::common::utils
    aos::common::version
    aos::core::cm::alerts
    aos::core::cm::imagemanager
    aos::core::cm::launcher
    aos::core::cm::monitoring
    aos::core::cm::nodeinfoprovider
    aos::core::cm::storagestate
    aos::core::cm::unitconfig
    aos::core::cm::updatemanager
    aos::core::common::crypto
    aos::core::common::pkcs11
    aos::core::common::spaceallocator
    aos::core::common::version
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
