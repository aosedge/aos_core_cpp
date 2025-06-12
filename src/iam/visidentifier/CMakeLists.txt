#
# Copyright (C) 2025 EPAM Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

set(TARGET_NAME visidentifier)

# ######################################################################################################################
# Sources
# ######################################################################################################################

set(SOURCES pocowsclient.cpp visidentifier.cpp vismessage.cpp wsclientevent.cpp wspendingrequests.cpp)

# ######################################################################################################################
# Libraries
# ######################################################################################################################

set(LIBRARIES
    aos::common::utils
    aos::common::logger
    ${TARGET_NAMESPACE}::config
    aoscommon
    Poco::Foundation
    Poco::Crypto
    Poco::Net
    Poco::NetSSL
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
