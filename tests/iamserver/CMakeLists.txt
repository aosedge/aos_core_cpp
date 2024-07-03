#
# Copyright (C) 2024 Renesas Electronics Corporation.
# Copyright (C) 2024 EPAM Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

set(TARGET iamserver_test)

# ######################################################################################################################
# Sources
# ######################################################################################################################

set(SOURCES iamserver_test.cpp nodecontroller_test.cpp protectedmessagehandler_test.cpp publicmessagehandler_test.cpp
            stubs/storagestub.cpp
)

# ######################################################################################################################
# Target
# ######################################################################################################################

add_executable(${TARGET} ${SOURCES})

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

find_package(libp11)

set(OPENSSL_CONF "${CMAKE_CURRENT_BINARY_DIR}/openssl.conf")

file(COPY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/openssl.conf" "${OPENSSL_CONF}")
file(APPEND "${OPENSSL_CONF}"
     "\n[pkcs11_section]\n" "engine_id = pkcs11\n" "dynamic_path = ${libp11_LIB_DIRS_DEBUG}/libpkcs11.so\n"
     "MODULE_PATH = ${SOFTHSM2_LIB}\n" "init = 0\n"
)

# ######################################################################################################################
# Libraries
# ######################################################################################################################

gtest_discover_tests(
    ${TARGET} PROPERTIES ENVIRONMENT SOFTHSM2_CONF=${CMAKE_CURRENT_BINARY_DIR}/softhsm/softhsm2.conf ENVIRONMENT
                         OPENSSL_CONF=${OPENSSL_CONF}
)

target_link_libraries(${TARGET} iamserver iamclient aoscoretestutils mbedtls GTest::gmock_main)
