#
# Copyright (C) 2024 EPAM Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

set(TARGET_NAME smclient)

# ######################################################################################################################
# Sources
# ######################################################################################################################

set(SOURCES smclient.cpp)

# ######################################################################################################################
# Libraries
# ######################################################################################################################

set(LIBRARIES aoscommon aossm ${COMMON_NAMESPACE}::iamclient ${COMMON_NAMESPACE}::pbconvert ${COMMON_NAMESPACE}::utils
              Poco::Util
)

if(WITH_SM_API)
    list(APPEND LIBRARIES aoscoreapi-gen-sm)
endif()

# ######################################################################################################################
# Target
# ######################################################################################################################

set(LOG_MODULE_NAME smclient)

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
