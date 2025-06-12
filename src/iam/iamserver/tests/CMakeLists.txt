#
# Copyright (C) 2025 EPAM Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

set(TARGET_NAME iamserver_test)

# ######################################################################################################################
# Sources
# ######################################################################################################################

set(SOURCES iamserver_test.cpp nodecontroller_test.cpp protectedmessagehandler_test.cpp publicmessagehandler_test.cpp
            stubs/storagestub.cpp
)

# ######################################################################################################################
# Libraries
# ######################################################################################################################

set(LIBRARIES ${TARGET_NAMESPACE}::iamserver ${TARGET_NAMESPACE}::iamclient aosiam aostestcore aostestutils
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

find_package(libp11)

set(OPENSSL_CONF "${CMAKE_CURRENT_BINARY_DIR}/openssl.conf")

file(COPY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/openssl.conf" "${OPENSSL_CONF}")
file(APPEND "${OPENSSL_CONF}"
     "\n[pkcs11_section]\n" "engine_id = pkcs11\n" "dynamic_path = ${libp11_LIB_DIRS_DEBUG}/libpkcs11.so\n"
     "MODULE_PATH = ${SOFTHSM2_LIB}\n" "init = 0\n"
)
