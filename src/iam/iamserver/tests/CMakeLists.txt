#
# Copyright (C) 2025 EPAM Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

set(TARGET_NAME iamserver_test)

# ######################################################################################################################
# Sources
# ######################################################################################################################

set(SOURCES iamserver.cpp nodecontroller.cpp protectedmessagehandler.cpp publicmessagehandler.cpp stubs/storagestub.cpp)

# ######################################################################################################################
# Libraries
# ######################################################################################################################

set(LIBRARIES aos::iam::iamserver aos::iam::iamclient aos::common::tests::utils aos::core::common::tests::crypto
              GTest::gmock_main
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

set(OPENSSL_CONF "${CMAKE_CURRENT_BINARY_DIR}/openssl.conf")

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
