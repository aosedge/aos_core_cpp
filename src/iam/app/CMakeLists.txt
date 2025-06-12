#
# Copyright (C) 2025 EPAM Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

set(TARGET_NAME service)

# ######################################################################################################################
# Sources
# ######################################################################################################################

set(SOURCES app.cpp main.cpp)

# ######################################################################################################################
# Libraries
# ######################################################################################################################

set(LIBRARIES
    ${TARGET_NAMESPACE}::config
    ${TARGET_NAMESPACE}::database
    ${TARGET_NAMESPACE}::fileidentifier
    ${TARGET_NAMESPACE}::iamclient
    ${TARGET_NAMESPACE}::iamserver
    ${TARGET_NAMESPACE}::nodeinfoprovider
    ${TARGET_NAMESPACE}::visidentifier
    aos::common::version
    aosiam
)

# ######################################################################################################################
# Includes
# ######################################################################################################################

set(INCLUDES ${CMAKE_CURRENT_BINARY_DIR})

# ######################################################################################################################
# Target
# ######################################################################################################################

add_exec(
    TARGET_NAME
    ${TARGET_NAME}
    INCLUDES
    ${INCLUDES}
    STACK_USAGE
    ${AOS_STACK_USAGE}
    SOURCES
    ${SOURCES}
    LIBRARIES
    ${LIBRARIES}
)
