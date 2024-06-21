#
# Copyright (C) 2024 Renesas Electronics Corporation.
# Copyright (C) 2024 EPAM Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

cmake_minimum_required(VERSION 3.19)

project("aos_core_common_cpp")

# ######################################################################################################################
# Options
# ######################################################################################################################

option(WITH_CORELIB "build with corelib" OFF)

option(WITH_TEST "build with test" OFF)
option(WITH_COVERAGE "build with coverage" OFF)
option(WITH_DOC "build with documenation" OFF)

message(STATUS)
message(STATUS "${CMAKE_PROJECT_NAME} configuration:")
message(STATUS "CMAKE_BUILD_TYPE              = ${CMAKE_BUILD_TYPE}")
message(STATUS "CMAKE_INSTALL_PREFIX          = ${CMAKE_INSTALL_PREFIX}")
message(STATUS)
message(STATUS "WITH_CORELIB                  = ${WITH_CORELIB}")
message(STATUS)
message(STATUS "WITH_TEST                     = ${WITH_TEST}")
message(STATUS "WITH_COVERAGE                 = ${WITH_COVERAGE}")
message(STATUS "WITH_DOC                      = ${WITH_DOC}")
message(STATUS)

# ######################################################################################################################
# Compiler flags
# ######################################################################################################################

add_compile_options(-fPIC -Wall -Werror -Wextra -Wpedantic)
set(CMAKE_CXX_STANDARD 17)

# ######################################################################################################################
# Dependencies
# ######################################################################################################################

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

# Aos core lib
include(AosCoreLib)

# gRPC lib
find_package(gRPC REQUIRED)

# Poco lib
find_package(Poco REQUIRED JSON DataSQLite)

# Systemd lib
find_package(PkgConfig REQUIRED)
pkg_check_modules(Systemd libsystemd REQUIRED)

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
# Includes
# ######################################################################################################################

include_directories(include)

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
