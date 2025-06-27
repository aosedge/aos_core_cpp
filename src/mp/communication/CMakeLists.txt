#
# Copyright (C) 2024 EPAM Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

# ######################################################################################################################
# Target name
# ######################################################################################################################

set(TARGET_NAME communication)

# ######################################################################################################################
# Sources
# ######################################################################################################################

set(SOURCES cmconnection.cpp communicationchannel.cpp communicationmanager.cpp iamconnection.cpp securechannel.cpp)

if(WITH_VCHAN)
    list(APPEND SOURCES vchan.cpp)
else()
    list(APPEND SOURCES socket.cpp)
endif()

# ######################################################################################################################
# Compile options
# ######################################################################################################################

set(COMPILE_OPTIONS -Wno-deprecated-declarations)

# ######################################################################################################################
# Libraries
# ######################################################################################################################

set(LIBRARIES
    aos::mp::config
    aos::mp::filechunker
    aos::mp::imageunpacker
    aos::mp::logprovider
    aos::common::logger
    aos::common::pbconvert
    aoscommon
    aos::api::sm
    aos::api::protocol
    OpenSSL::Crypto
    OpenSSL::SSL
    Poco::Foundation
    Poco::Net
)

if(WITH_VCHAN)
    list(APPEND LIBRARIES Xenvchan::Xenvchan)
endif()

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
    COMPILE_OPTIONS
    ${COMPILE_OPTIONS}
    LIBRARIES
    ${LIBRARIES}
)

# ######################################################################################################################
# Tests
# ######################################################################################################################

if(WITH_TEST)
    add_subdirectory(tests)
endif()
