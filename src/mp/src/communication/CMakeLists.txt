#
# Copyright (C) 2024 EPAM Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

set(TARGET communication)

# ######################################################################################################################
# Compiler flags
# ######################################################################################################################

add_definitions(-DLOG_MODULE="communication")

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
# Includes
# ######################################################################################################################

# ######################################################################################################################
# Target
# ######################################################################################################################

add_library(${TARGET} STATIC ${SOURCES})

target_compile_options(${TARGET} PRIVATE -Wno-deprecated-declarations)

# ######################################################################################################################
# Libraries
# ######################################################################################################################

target_link_libraries(
    ${TARGET}
    PUBLIC OpenSSL::Crypto
           OpenSSL::SSL
           Poco::Foundation
           Poco::Net
           aoscommon
           aoscoreapi-gen-sm
           iamclient
           aosprotocol
           aosutils
           filechunker
           imageunpacker
           logprovider
)

if(WITH_VCHAN)
    target_link_libraries(${TARGET} PUBLIC Xenvchan::Xenvchan)
endif()
