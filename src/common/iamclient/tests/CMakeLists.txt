#
# Copyright (C) 2024 Renesas Electronics Corporation.
# Copyright (C) 2024 EPAM Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

# ######################################################################################################################
# Target name
# ######################################################################################################################

set(TARGET_NAME iamclient_test)

# ######################################################################################################################
# Sources
# ######################################################################################################################

set(SOURCES
    certificateservice.cpp
    nodesservice.cpp
    permservice.cpp
    provisioningservice.cpp
    publiccertservice.cpp
    publiccurrentnodeservice.cpp
    publicidentityservice.cpp
    publicnodeservice.cpp
    publicpermservice.cpp
)

# ######################################################################################################################
# Libraries
# ######################################################################################################################

set(LIBRARIES aos::common::iamclient aos::common::tests::mocks Poco::Foundation GTest::gmock_main)

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
