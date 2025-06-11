#
# Copyright (C) 2024 Renesas Electronics Corporation.
# Copyright (C) 2024 EPAM Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

set(TARGET_NAME aos_servicemanager)

# ######################################################################################################################
# Dependencies
# ######################################################################################################################

find_package(Poco REQUIRED Foundation Util JSON DataSQLite)

# ######################################################################################################################
# Versioning
# ######################################################################################################################

find_package(Git)

if(NOT GIT_FOUND)
    set(GIT_EXECUTABLE git)
endif()

execute_process(
    OUTPUT_FILE ${CMAKE_CURRENT_BINARY_DIR}/version.hpp
    COMMAND
        ${CMAKE_COMMAND} -D GIT_EXECUTABLE=${GIT_EXECUTABLE} -D INPUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/version.hpp.in
        -D OUTPUT_FILE=${CMAKE_CURRENT_BINARY_DIR}/version.hpp -D GIT_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR} -P
        ${AOS_CORE_LIB_DIR}/cmake/GenerateVersion.cmake
)

# ######################################################################################################################
# Includes
# ######################################################################################################################

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

# ######################################################################################################################
# Target aliases
# ######################################################################################################################

set(COMMON_NAMESPACE ${TARGET_NAMESPACE}::common)
set(COMMON_TESTS_NAMESPACE ${COMMON_NAMESPACE}::tests)
set(TARGET_NAMESPACE ${TARGET_NAMESPACE}::sm)
set(TARGET_PREFIX ${TARGET_PREFIX}_sm)

set(AOS_SM_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# ######################################################################################################################
# Modules
# ######################################################################################################################

add_subdirectory(alerts)
add_subdirectory(app)
add_subdirectory(config)
add_subdirectory(database)
add_subdirectory(image)
add_subdirectory(launcher)
add_subdirectory(logprovider)
add_subdirectory(monitoring)
add_subdirectory(networkmanager)
add_subdirectory(resourcemanager)
add_subdirectory(runner)
add_subdirectory(smclient)
add_subdirectory(utils)

if(WITH_TEST)
    add_subdirectory(alerts/tests)
    add_subdirectory(config/tests)
    add_subdirectory(database/tests)
    add_subdirectory(image/tests)
    add_subdirectory(launcher/tests)
    add_subdirectory(logprovider/tests)
    add_subdirectory(monitoring/tests)
    add_subdirectory(networkmanager/tests)
    add_subdirectory(resourcemanager/tests)
    add_subdirectory(runner/tests)
    add_subdirectory(smclient/tests)
endif()

# ######################################################################################################################
# Sources
# ######################################################################################################################

set(SOURCES main.cpp)

# ######################################################################################################################
# Libraries
# ######################################################################################################################

set(LIBRARIES aoscommon ${TARGET_NAMESPACE}::app)
set(LINK_OPTIONS -rdynamic)

# ######################################################################################################################
# Target
# ######################################################################################################################

add_exec(
    TARGET_NAME
    ${TARGET_NAME}
    STACK_USAGE
    ${AOS_STACK_USAGE}
    SOURCES
    ${SOURCES}
    LIBRARIES
    ${LIBRARIES}
    LINK_OPTIONS
    ${LINK_OPTIONS}
)
