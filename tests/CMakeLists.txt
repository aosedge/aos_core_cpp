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
include_directories(include)

add_subdirectory(iam)

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
    visidentifier/pocowsclient_test.cpp
    visidentifier/visidentifier_test.cpp
    visidentifier/vismessage_test.cpp
    visidentifier/visserver.cpp
)

# ######################################################################################################################
# Target
# ######################################################################################################################

add_executable(${TARGET} ${SOURCES})

gtest_discover_tests(${TARGET})

# ######################################################################################################################
# Labraries
# ######################################################################################################################

target_link_libraries(
    ${TARGET}
    config
    database
    iamclient
    logger
    utils
    visidentifier
    aoscoreapi-gen-objects
    aoscommon
    Poco::Crypto
    Poco::Net
    Poco::NetSSL
    Poco::JSON
    Poco::Util
    GTest::gmock_main
)
