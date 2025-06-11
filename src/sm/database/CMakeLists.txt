#
# Copyright (C) 2024 EPAM Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

set(TARGET_NAME database)

# ######################################################################################################################
# Sources
# ######################################################################################################################

set(SOURCES database.cpp)

# ######################################################################################################################
# Libraries
# ######################################################################################################################

set(LIBRARIES aoscommon ${COMMON_NAMESPACE}::utils ${COMMON_NAMESPACE}::migration ${TARGET_NAMESPACE}::config
              Poco::DataSQLite Poco::JSON
)

# ######################################################################################################################
# Target
# ######################################################################################################################

set(LOG_MODULE_NAME database)

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
