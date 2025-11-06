#
# Copyright (C) 2025 EPAM Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

set(TARGET_NAME communication_test)

# ######################################################################################################################
# Sources
# ######################################################################################################################

set(SOURCES communication.cpp)

# ######################################################################################################################
# Libraries
# ######################################################################################################################

set(LIBRARIES
    aos::cm::communication
    aos::cm::communication::cloudprotocol
    aos::common::tests::stubs
    aos::common::tests::utils
    aos::core::common::tests::crypto
    aos::core::common::tests::utils
    GTest::gmock_main
)

# ######################################################################################################################
# Test properties
# ######################################################################################################################

set(OPENSSL_CONF "${CMAKE_CURRENT_BINARY_DIR}/openssl.conf")

set(PROPERTIES "ENVIRONMENT" "SOFTHSM2_CONF=${CMAKE_CURRENT_BINARY_DIR}/softhsm/softhsm2.conf" "ENVIRONMENT"
               "OPENSSL_CONF=${OPENSSL_CONF}"
)

# ######################################################################################################################
# Target
# ######################################################################################################################

add_test(
    TARGET_NAME
    ${TARGET_NAME}
    LOG_MODULE
    SOURCES
    ${SOURCES}
    LIBRARIES
    ${LIBRARIES}
    PROPERTIES
    ${PROPERTIES}
)

# ######################################################################################################################
# Setup softhsm
# ######################################################################################################################

include(cmake/softhsm.cmake)

createsofthsmtestenv(${TARGET_PREFIX}_${TARGET_NAME} "${CMAKE_CURRENT_BINARY_DIR}/softhsm")

# ######################################################################################################################
# Gen certificates
# ######################################################################################################################

include(cmake/gencertificates.cmake)

gencertificates(${TARGET_PREFIX}_${TARGET_NAME} "${CMAKE_CURRENT_BINARY_DIR}/certificates")

# ######################################################################################################################
# Setup openssl.conf
# ######################################################################################################################

find_package(pkcs11provider)
find_package(OpenSSL)

file(COPY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/openssl.conf" "${OPENSSL_CONF}")
file(
    APPEND "${OPENSSL_CONF}"
    "\n[legacy_sect]\n"
    "activate = 1\n"
    "module = ${openssl_LIB_DIRS_DEBUG}/ossl-modules/legacy.so\n"
    "\n[pkcs11_sect]\n"
    "module = ${pkcs11provider_PACKAGE_FOLDER_DEBUG}/pkcs11.so\n"
    "pkcs11-module-path = ${SOFTHSM2_LIB}\n"
    "activate = 1\n"
)
