#
# Copyright (C) 2024 Renesas Electronics Corporation.
# Copyright (C) 2024 EPAM Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

# ######################################################################################################################
# Dependencies
# ######################################################################################################################

find_package(CURL REQUIRED)
find_package(gRPC REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(Poco REQUIRED JSON DataSQLite Foundation Util)

pkg_check_modules(Libnl libnl-3.0 libnl-route-3.0 REQUIRED)
pkg_check_modules(Systemd libsystemd REQUIRED)

# ######################################################################################################################
# Target properties
# ######################################################################################################################

set(TARGET_NAMESPACE ${TARGET_NAMESPACE}::common)
set(TARGET_PREFIX ${TARGET_PREFIX}_common)

# ######################################################################################################################
# Modules
# ######################################################################################################################

add_subdirectory(config)
add_subdirectory(downloader)
add_subdirectory(fileserver)
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
add_subdirectory(version)

if(WITH_TEST)
    add_subdirectory(tests)
endif()
