#
# Copyright (C) 2024 Renesas Electronics Corporation.
# Copyright (C) 2024 EPAM Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

set(TARGET aos_iamanager_test)

# ######################################################################################################################
# Defines
# ######################################################################################################################

# ######################################################################################################################
# Includes
# ######################################################################################################################

include(create-certs.cmake)
include_directories(${CMAKE_SOURCE_DIR}/src)

# ######################################################################################################################
# Sources
# ######################################################################################################################

set(SOURCES
    config/config_test.cpp
    database/database_test.cpp
    iam_client/iamclient_test.cpp
    utils/exception_test.cpp
    utils/json_test.cpp
    utils/time_test.cpp
    utils/exception_test.cpp
    utils/json_test.cpp
    visidentifier/pocowsclient_test.cpp
    visidentifier/visconfig_test.cpp
    visidentifier/visidentifier_test.cpp
    visidentifier/vismessage_test.cpp
    visidentifier/visserver.cpp
)

# ######################################################################################################################
# Target
# ######################################################################################################################

add_executable(${TARGET} ${SOURCES})
target_include_directories(${TARGET} PRIVATE include)

gtest_discover_tests(${TARGET})

# ######################################################################################################################
# Labraries
# ######################################################################################################################

target_link_libraries(
    ${TARGET}
    logger
    visidentifier
    aoscommon
    config
    database
    iamclient
    aoscoreapi-gen-objects
    utils
    Poco::Crypto
    Poco::Net
    Poco::NetSSL
    Poco::JSON
    Poco::Util
    GTest::gmock_main
)
