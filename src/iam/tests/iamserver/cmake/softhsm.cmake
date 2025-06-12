#
# Copyright (C) 2024 Renesas Electronics Corporation.
# Copyright (C) 2024 EPAM Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

#
# Creates softhsm test environment
#

set(AOSCORE_UTILS_LIB_DIR ${CMAKE_CURRENT_LIST_DIR})

find_library(
    SOFTHSM2_LIB
    NAMES softhsm2
    PATH_SUFFIXES softhsm
)

if(NOT SOFTHSM2_LIB)
    message(FATAL_ERROR "softhsm2 library not found")
endif()

function(createsofthsmtestenv TARGET SOFTHSM_BASE_DIR)
    file(MAKE_DIRECTORY ${SOFTHSM_BASE_DIR}/tokens/)

    configure_file(${AOSCORE_UTILS_LIB_DIR}/softhsm2.conf ${SOFTHSM_BASE_DIR}/softhsm2.conf COPYONLY)
    file(APPEND ${SOFTHSM_BASE_DIR}/softhsm2.conf "directories.tokendir = ${SOFTHSM_BASE_DIR}/tokens/\n")

    target_compile_definitions(
        ${TARGET} PUBLIC SOFTHSM_BASE_IAM_DIR="${SOFTHSM_BASE_DIR}" SOFTHSM2_LIB="${SOFTHSM2_LIB}"
    )
endfunction()
