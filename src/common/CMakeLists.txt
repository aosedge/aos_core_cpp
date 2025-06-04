#
# Copyright (C) 2024 Renesas Electronics Corporation.
# Copyright (C) 2024 EPAM Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

# ######################################################################################################################
# Dependencies
# ######################################################################################################################

# gRPC lib
find_package(gRPC REQUIRED)

# Poco lib
find_package(Poco REQUIRED JSON DataSQLite Foundation Util)

# Find the libcurl package
find_package(CURL REQUIRED)

# Systemd lib
find_package(PkgConfig REQUIRED)
pkg_check_modules(Systemd libsystemd REQUIRED)

# Add libnl dependencies
find_package(PkgConfig REQUIRED)
pkg_check_modules(Libnl libnl-3.0 libnl-route-3.0 REQUIRED)

# ######################################################################################################################
# Defines
# ######################################################################################################################

# ######################################################################################################################
# Includes
# ######################################################################################################################

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# ######################################################################################################################
# Modules
# ######################################################################################################################

add_subdirectory(downloader)
add_subdirectory(iamclient)
add_subdirectory(jsonprovider)
add_subdirectory(logger)
add_subdirectory(logprovider)
add_subdirectory(migration)
add_subdirectory(network)
add_subdirectory(ocispec)

if(WITH_IAM_API OR WITH_SM_API)
    add_subdirectory(pbconvert)
endif()

add_subdirectory(utils)

if(WITH_TEST)
    add_subdirectory(tests)

    add_subdirectory(downloader/tests)
    add_subdirectory(iamclient/tests)
    add_subdirectory(jsonprovider/tests)
    add_subdirectory(logprovider/tests)
    add_subdirectory(migration/tests)
    add_subdirectory(ocispec/tests)

    if(WITH_IAM_API OR WITH_SM_API)
        add_subdirectory(pbconvert/tests)
    endif()

    add_subdirectory(utils/tests)
endif()
