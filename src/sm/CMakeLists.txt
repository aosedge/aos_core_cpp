#
# Copyright (C) 2024 EPAM Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

# ######################################################################################################################
# Options
# ######################################################################################################################

option(WITH_RUNTIME_BOOT "build with boot runtime" ON)
option(WITH_RUNTIME_ROOTFS "build with rootfs runtime" ON)
option(WITH_RUNTIME_CONTAINER "build with container runtime" ON)

message(STATUS)
message(STATUS "WITH_RUNTIME_BOOT             = ${WITH_RUNTIME_BOOT}")
message(STATUS "WITH_RUNTIME_ROOTFS           = ${WITH_RUNTIME_ROOTFS}")
message(STATUS "WITH_RUNTIME_CONTAINER        = ${WITH_RUNTIME_CONTAINER}")
message(STATUS)

if(WITH_RUNTIME_BOOT)
    add_definitions(-DWITH_RUNTIME_BOOT)
endif()

if(WITH_RUNTIME_ROOTFS)
    add_definitions(-DWITH_RUNTIME_ROOTFS)
endif()

if(WITH_RUNTIME_CONTAINER)
    add_definitions(-DWITH_RUNTIME_CONTAINER)
endif()

# ######################################################################################################################
# Dependencies
# ######################################################################################################################

find_package(PkgConfig REQUIRED)
find_package(Poco REQUIRED Foundation Util JSON DataSQLite)

pkg_check_modules(Libnftables libnftables REQUIRED)
pkg_check_modules(Systemd libsystemd REQUIRED)

# ######################################################################################################################
# Target properties
# ######################################################################################################################

set(TARGET_NAMESPACE ${TARGET_NAMESPACE}::sm)
set(TARGET_PREFIX ${TARGET_PREFIX}_sm)

# ######################################################################################################################
# Modules
# ######################################################################################################################

add_subdirectory(alerts)
add_subdirectory(app)
add_subdirectory(config)
add_subdirectory(database)
add_subdirectory(iamclient)
add_subdirectory(imagemanager)
add_subdirectory(launcher)
add_subdirectory(logprovider)
add_subdirectory(monitoring)
add_subdirectory(networkmanager)
add_subdirectory(nftables)
add_subdirectory(resourcemanager)
add_subdirectory(smclient)
add_subdirectory(utils)

# ######################################################################################################################
# Tests
# ######################################################################################################################

if(WITH_TEST)
    add_subdirectory(tests)
endif()

# ######################################################################################################################
# Install conan runtime dependencies
# ######################################################################################################################

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

    install(CODE "find_program(PATCHELF_EXECUTABLE patchelf REQUIRED)
         execute_process(COMMAND \${PATCHELF_EXECUTABLE} --set-rpath \"${AOS_CRUN_RPATH}\"
             \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin/crun\")"
    )
endif()
