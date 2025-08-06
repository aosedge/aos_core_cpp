#
# Copyright (C) 2025 EPAM Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

set(TARGET_NAME communication)

# ######################################################################################################################
# Sources
# ######################################################################################################################

set(SOURCES communication.cpp)

# ######################################################################################################################
# Libraries
# ######################################################################################################################

set(LIBRARIES
    aos::common::utils
    aos::common::cloudprotocol
    aos::common::config
    aos::core::common::crypto
    Poco::Crypto
    Poco::Foundation
    Poco::Net
    Poco::NetSSL
    Poco::JSON
)

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
