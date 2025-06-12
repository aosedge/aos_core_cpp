#
# Copyright (C) 2025 EPAM Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

# ######################################################################################################################
# Target name
# ######################################################################################################################

set(TARGET_NAME version)

# ######################################################################################################################
# Dependencies
# ######################################################################################################################

find_package(Git)

if(NOT GIT_FOUND)
    set(GIT_EXECUTABLE git)
endif()

set(INCLUDE_PATH ${CMAKE_CURRENT_BINARY_DIR}/include)
set(HEADER_PATH ${INCLUDE_PATH}/common/version)

file(MAKE_DIRECTORY ${HEADER_PATH})

execute_process(
    OUTPUT_FILE ${HEADER_PATH}/version.hpp
    COMMAND
        ${CMAKE_COMMAND} -D GIT_EXECUTABLE=${GIT_EXECUTABLE} -D INPUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/version.hpp.in
        -D OUTPUT_FILE=${HEADER_PATH}/version.hpp -D GIT_SOURCE_DIR=${PROJECT_SOURCE_DIR} -P
        ${PROJECT_SOURCE_DIR}/cmake/GenerateVersion.cmake
)

# ######################################################################################################################
# Includes
# ######################################################################################################################

set(INCLUDES ${INCLUDE_PATH})

# ######################################################################################################################
# Target
# ######################################################################################################################

add_module(TARGET_NAME ${TARGET_NAME} INCLUDES ${INCLUDES})
