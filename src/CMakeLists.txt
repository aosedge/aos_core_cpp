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

# ######################################################################################################################
# Defines
# ######################################################################################################################

# ######################################################################################################################
# Includes
# ######################################################################################################################

include_directories(${AOS_CORE_COMMON_LIB_DIR}/include)

# ######################################################################################################################
# Modules
# ######################################################################################################################

add_subdirectory(jsonprovider)
add_subdirectory(logger)
add_subdirectory(migration)
add_subdirectory(ocispec)
add_subdirectory(utils)
add_subdirectory(iamclient)
add_subdirectory(downloader)
add_subdirectory(network)

if(WITH_IAM_API OR WITH_SM_API)
    add_subdirectory(pbconvert)
endif()
