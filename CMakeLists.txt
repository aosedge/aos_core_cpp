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

set(AOS_CORE_DIR
    "core"
    CACHE STRING "path where Aos core lib, core API and other dependencies are located"
)

option(WITH_TEST "build with test" OFF)
option(WITH_COVERAGE "build with coverage" OFF)
option(WITH_DOC "build with documentation" OFF)

message(STATUS)
message(STATUS "${CMAKE_PROJECT_NAME} configuration:")
message(STATUS "CMAKE_BUILD_TYPE              = ${CMAKE_BUILD_TYPE}")
message(STATUS "CMAKE_INSTALL_PREFIX          = ${CMAKE_INSTALL_PREFIX}")
message(STATUS)
message(STATUS "AOS_CORE_DIR                  = ${AOS_CORE_DIR}")
message(STATUS)
message(STATUS "WITH_TEST                     = ${WITH_TEST}")
message(STATUS "WITH_COVERAGE                 = ${WITH_COVERAGE}")
message(STATUS "WITH_DOC                      = ${WITH_DOC}")
message(STATUS)

# ######################################################################################################################
# External project options
# ######################################################################################################################

# aos_core_api options
set(WITH_IAM_API ON)
set(WITH_SM_API ON)

# aos_core_lib_cpp options
set(WITH_MBEDTLS OFF)
set(WITH_OPENSSL ON)

# ######################################################################################################################
# Config
# ######################################################################################################################

set(AOS_STACK_USAGE 4096)

get_filename_component(AOS_CORE_LIB_DIR ${AOS_CORE_DIR}/aos_core_lib_cpp REALPATH BASE_DIR "${CMAKE_BINARY_DIR}")
get_filename_component(AOS_CORE_API_DIR ${AOS_CORE_DIR}/aos_core_api REALPATH BASE_DIR "${CMAKE_BINARY_DIR}")

# ######################################################################################################################
# Compiler flags
# ######################################################################################################################

add_compile_options(-fPIC -Wall -Werror -Wextra -Wpedantic -Wno-format-truncation)
set(CMAKE_CXX_STANDARD 17)

# ######################################################################################################################
# Dependencies
# ######################################################################################################################

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

if(WITH_TEST)
    find_package(GTest REQUIRED)

    include(GoogleTest)

    enable_testing()
endif()

if(WITH_COVERAGE)
    include(CodeCoverage)

    append_coverage_compiler_flags()

    set(COVERAGE_EXCLUDES "build/*" "tests/*" "/usr/*")

    setup_target_for_coverage_lcov(
        NAME
        coverage
        EXECUTABLE
        ctest
        SONARQUBE
        LCOV_ARGS
        --ignore-errors
        mismatch,negative
    )
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

# ######################################################################################################################
# External projects
# ######################################################################################################################

include(FetchContent)

if(NOT EXISTS ${AOS_CORE_LIB_DIR})
    FetchContent_Populate(
        aos_core_lib_cpp
        GIT_REPOSITORY https://github.com/aosedge/aos_core_lib_cpp.git
        GIT_TAG develop
        SOURCE_DIR ${AOS_CORE_LIB_DIR}
    )
endif()

if(NOT EXISTS ${AOS_CORE_API_DIR})
    FetchContent_Populate(
        aos_core_api
        GIT_REPOSITORY https://github.com/aosedge/aos_core_api.git
        GIT_TAG develop
        SOURCE_DIR ${AOS_CORE_API_DIR}
    )
endif()

# ######################################################################################################################
# Targets
# ######################################################################################################################

add_subdirectory(src)

# External projects

add_subdirectory(${AOS_CORE_API_DIR} aos_core_api)
add_subdirectory(${AOS_CORE_LIB_DIR}/src/common aos_core_lib/common)

if(WITH_TEST)
    add_subdirectory(${AOS_CORE_LIB_DIR}/tests/utils aos_core_lib_cpp/tests/utils)
    add_subdirectory(${AOS_CORE_LIB_DIR}/tests/include aos_core_lib_cpp/tests/include)
endif()
