#
# Copyright (C) 2024 EPAM Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

set(TARGET_NAME app)

# ######################################################################################################################
# Sources
# ######################################################################################################################

set(SOURCES app.cpp aoscore.cpp main.cpp)

# ######################################################################################################################
# Libraries
# ######################################################################################################################

set(LIBRARIES
    Poco::Util
    aos::core::common::crypto
    aos::core::common::monitoring
    aos::core::common::pkcs11
    aos::core::common::version
    aos::core::sm::imagemanager
    aos::core::sm::launcher
    aos::core::sm::networkmanager
    aos::core::sm::nodeconfig
    aos::common::downloader
    aos::common::iamclient
    aos::common::jsonprovider
    aos::common::logger
    aos::common::network
    aos::common::ocispec
    aos::common::version
    aos::sm::alerts
    aos::sm::config
    aos::sm::database
    aos::sm::iamclient
    aos::sm::imagemanager
    aos::sm::launcher
    aos::sm::logprovider
    aos::sm::monitoring
    aos::sm::networkmanager
    aos::sm::nftables
    aos::sm::resourcemanager
    aos::sm::smclient
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

if(WITH_AOS_INSTALL)
    if(WITH_RUNTIME_CONTAINER)
        find_package(crun REQUIRED)

        install(
            DIRECTORY ${crun_PACKAGE_FOLDER_${AOS_CONAN_CONFIG}}/lib/
            DESTINATION lib
            FILES_MATCHING
            PATTERN "*.so*"
        )

        install(PROGRAMS ${crun_PACKAGE_FOLDER_${AOS_CONAN_CONFIG}}/bin/crun DESTINATION bin)

        # crun isn't a CMake target, so install(TARGETS)'s automatic rpath handling doesn't apply to it, and its
        # upstream build doesn't reserve enough rpath room for CMake's own file(RPATH_SET) to grow it in place.
        # patchelf can resize the binary to fit. The absolute entry (not just $ORIGIN) is required because crun
        # re-execs itself from a memfd copy of itself on load, which breaks $ORIGIN-relative resolution.
        set(AOS_CRUN_RPATH "$ORIGIN/../lib:${CMAKE_INSTALL_PREFIX}/lib")

        find_program(PATCHELF_EXECUTABLE patchelf REQUIRED)

        install(CODE "execute_process(COMMAND \${PATCHELF_EXECUTABLE} --set-rpath \"${AOS_CRUN_RPATH}\"
             \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin/crun\")"
        )
    endif()

    configure_file(${PROJECT_SOURCE_DIR}/systemd/aos-sm.service.in ${CMAKE_CURRENT_BINARY_DIR}/aos-sm.service @ONLY)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/aos-sm.service DESTINATION lib/systemd/system)

    configure_file(${PROJECT_SOURCE_DIR}/config/sm.cfg.in ${CMAKE_CURRENT_BINARY_DIR}/sm.cfg @ONLY)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/sm.cfg DESTINATION etc/aos)

    install(FILES ${PROJECT_SOURCE_DIR}/config/aos-nftables.conf DESTINATION etc/aos)
    configure_file(
        ${PROJECT_SOURCE_DIR}/systemd/aos-nftables.service.in ${CMAKE_CURRENT_BINARY_DIR}/aos-nftables.service @ONLY
    )
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/aos-nftables.service DESTINATION lib/systemd/system)
endif()
