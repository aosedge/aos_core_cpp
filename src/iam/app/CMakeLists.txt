#
# Copyright (C) 2025 EPAM Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

set(TARGET_NAME app)

# ######################################################################################################################
# Sources
# ######################################################################################################################

set(SOURCES aoscore.cpp app.cpp main.cpp)

# ######################################################################################################################
# Libraries
# ######################################################################################################################

set(LIBRARIES
    aos::iam::config
    aos::iam::currentnode
    aos::iam::database
    aos::iam::iamclient
    aos::iam::iamserver
    aos::iam::identhandler
    aos::common::version
    aos::core::common::version
)

# ######################################################################################################################
# Includes
# ######################################################################################################################

set(INCLUDES ${CMAKE_CURRENT_BINARY_DIR})

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

if(WITH_AOS_INSTALL)
    configure_file(${PROJECT_SOURCE_DIR}/systemd/aos-iam.service.in ${CMAKE_CURRENT_BINARY_DIR}/aos-iam.service @ONLY)
    configure_file(
        ${PROJECT_SOURCE_DIR}/systemd/aos-iam-prov.service.in ${CMAKE_CURRENT_BINARY_DIR}/aos-iam-prov.service @ONLY
    )
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/aos-iam.service ${CMAKE_CURRENT_BINARY_DIR}/aos-iam-prov.service
            DESTINATION lib/systemd/system
    )

    configure_file(${PROJECT_SOURCE_DIR}/config/iam.cfg.in ${CMAKE_CURRENT_BINARY_DIR}/iam.cfg @ONLY)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/iam.cfg DESTINATION etc/aos)

    install(PROGRAMS ${PROJECT_SOURCE_DIR}/config/unit_model DESTINATION etc/aos)

    install(PROGRAMS ${PROJECT_SOURCE_DIR}/scripts/deprovision.sh ${PROJECT_SOURCE_DIR}/scripts/provfinish.sh
            DESTINATION libexec/aos
    )

    configure_file(${PROJECT_SOURCE_DIR}/config/aos-openssl.cnf.in ${CMAKE_CURRENT_BINARY_DIR}/aos-openssl.cnf @ONLY)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/aos-openssl.cnf DESTINATION etc/aos)
endif()
