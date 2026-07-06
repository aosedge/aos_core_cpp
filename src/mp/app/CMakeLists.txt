#
# Copyright (C) 2024 EPAM Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

# ######################################################################################################################
# Target name
# ######################################################################################################################

set(TARGET_NAME app)

# ######################################################################################################################
# Sources
# ######################################################################################################################

set(SOURCES app.cpp main.cpp)

# ######################################################################################################################
# Libraries
# ######################################################################################################################

set(LIBRARIES
    aos::mp::communication
    aos::mp::cmclient
    aos::mp::iamclient
    aos::mp::config
    aos::common::downloader
    aos::common::logger
    aos::common::version
    aos::core::common::crypto
    aos::core::common::pkcs11
    aos::core::common::version
    CURL::libcurl
    Poco::Foundation
    Poco::Util
)

# ######################################################################################################################
# Target
# ######################################################################################################################

add_exec(
    TARGET_NAME
    ${TARGET_NAME}
    LOG_MODULE
    STACK_USAGE
    ${AOS_STACK_USAGE}
    SOURCES
    ${SOURCES}
    LIBRARIES
    ${LIBRARIES}
    LINK_OPTIONS
    -rdynamic
)

# ######################################################################################################################
# Install
# ######################################################################################################################

install(TARGETS ${TARGET} RUNTIME DESTINATION bin)

configure_file(${PROJECT_SOURCE_DIR}/systemd/aos-mp.service.in ${CMAKE_CURRENT_BINARY_DIR}/aos-mp.service @ONLY)
configure_file(
    ${PROJECT_SOURCE_DIR}/systemd/aos-mp-prov.service.in ${CMAKE_CURRENT_BINARY_DIR}/aos-mp-prov.service @ONLY
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/aos-mp.service ${CMAKE_CURRENT_BINARY_DIR}/aos-mp-prov.service
        DESTINATION lib/systemd/system
)

