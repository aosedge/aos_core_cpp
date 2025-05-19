#
# Copyright (C) 2024 Renesas Electronics Corporation.
# Copyright (C) 2024 EPAM Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

set(TARGET communication_test)

# ######################################################################################################################
# Sources
# ######################################################################################################################

set(SOURCES communication_secure_test.cpp communication_open_test.cpp stubs/storagestub.cpp)

# ######################################################################################################################
# Target
# ######################################################################################################################

add_executable(${TARGET} ${SOURCES})
target_compile_options(${TARGET} PRIVATE -Wno-deprecated-declarations)

# ######################################################################################################################
# Setup softhsm
# ######################################################################################################################

include(cmake/softhsm.cmake)

createsofthsmtestenv(${TARGET} "${CMAKE_CURRENT_BINARY_DIR}/softhsm")

# ######################################################################################################################
# Gen certificates
# ######################################################################################################################

include(cmake/gencertificates.cmake)

gencertificates(${TARGET} "${CMAKE_CURRENT_BINARY_DIR}/certificates")

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

# ######################################################################################################################
# Libraries
# ######################################################################################################################

gtest_discover_tests(
    ${TARGET} PROPERTIES ENVIRONMENT SOFTHSM2_CONF=${CMAKE_CURRENT_BINARY_DIR}/softhsm/softhsm2.conf ENVIRONMENT
                         OPENSSL_CONF=${OPENSSL_CONF}
)

target_link_libraries(
    ${TARGET}
    GTest::gmock_main
    OpenSSL::Crypto
    OpenSSL::SSL
    aoscommon
    aosdownloader
    aostestutils
    aosiam
    aosutils
    communication
)
