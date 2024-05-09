#
# Copyright (C) 2024 Renesas Electronics Corporation.
# Copyright (C) 2024 EPAM Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

cmake_minimum_required(VERSION 3.19)

project("aos_core_iam_cpp")

# ######################################################################################################################
# Options
# ######################################################################################################################

option(WITH_TEST "build with test" OFF)
option(WITH_COVERAGE "build with coverage" OFF)
option(WITH_DOC "build with documenation" OFF)

message(STATUS)
message(STATUS "${CMAKE_PROJECT_NAME} configuration:")
message(STATUS "CMAKE_BUILD_TYPE              = ${CMAKE_BUILD_TYPE}")
message(STATUS "CMAKE_INSTALL_PREFIX          = ${CMAKE_INSTALL_PREFIX}")
message(STATUS)
message(STATUS "WITH_TEST                     = ${WITH_TEST}")
message(STATUS "WITH_COVERAGE                 = ${WITH_COVERAGE}")
message(STATUS "WITH_DOC                      = ${WITH_DOC}")
message(STATUS)

# ######################################################################################################################
# Compiler flags
# ######################################################################################################################

set(common_flags "-fPIC -Wall -Werror -Wextra -Wpedantic")

set(CMAKE_CXX_FLAGS "${common_flags} ${CMAKE_CXX_FLAGS}")
set(CMAKE_C_FLAGS "${common_flags} ${CMAKE_C_FLAGS}")
set(CMAKE_CXX_STANDARD 17)

# ######################################################################################################################
# Versioning
# ######################################################################################################################

find_package(Git)

if(NOT GIT_FOUND)
    set(GIT_EXECUTABLE git)
endif()

execute_process(
    OUTPUT_FILE ${CMAKE_BINARY_DIR}/version.hpp
    COMMAND
        ${CMAKE_COMMAND} -D GIT_EXECUTABLE=${GIT_EXECUTABLE} -D INPUT_FILE=${CMAKE_SOURCE_DIR}/src/version.hpp.in -D
        OUTPUT_FILE=${CMAKE_BINARY_DIR}/version.hpp -D GIT_SOURCE_DIR=${CMAKE_SOURCE_DIR} -P
        ${CMAKE_SOURCE_DIR}/cmake/GenerateVersion.cmake
)

include_directories(${CMAKE_BINARY_DIR})

# ######################################################################################################################
# Dependencies
# ######################################################################################################################

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

# Aos core lib
include(AosCoreLib)

# Aos core API
include(AosCoreAPI)

# Aos core common
include(AosCoreCommon)

# Poco lib
find_package(
    Poco
    REQUIRED
    Crypto
    DataSQLite
    Foundation
    JSON
    Net
    NetSSL
    Util
)

if(WITH_TEST)
    find_package(GTest REQUIRED)

    include(GoogleTest)

    enable_testing()
endif()

if(WITH_COVERAGE)
    include(CodeCoverage)

    append_coverage_compiler_flags()

    set(COVERAGE_EXCLUDES "build/*" "/usr/*" "*_test.cpp")

    setup_target_for_coverage_lcov(NAME coverage EXECUTABLE ctest)
endif()

# ######################################################################################################################
# Targets
# ######################################################################################################################

add_subdirectory(src)

if(WITH_TEST)
    add_subdirectory(tests)
endif()

# ######################################################################################################################
# Doc
# ######################################################################################################################

if(WITH_DOC)
    find_package(Doxygen)

    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/doxygen.cfg ${CMAKE_CURRENT_BINARY_DIR}/doxygen.cfg @ONLY)

    add_custom_target(
        doc
        ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/doxygen.cfg
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM
    )
endif()
