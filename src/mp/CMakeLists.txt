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
find_package(Poco REQUIRED JSON DataSQLite Foundation Util)

if(WITH_VCHAN)
    find_package(Xenvchan REQUIRED)
    if(XENVCHAN_FOUND)
        message(STATUS "Xenvchan found:")
        message(STATUS "  version: ${XENVCHAN_VERSION}")
        message(STATUS "  libraries: ${XENVCHAN_LIBRARIES}")
        message(STATUS "  include dirs: ${XENVCHAN_INCLUDE_DIRS}")
    else()
        message(FATAL_ERROR "Xenvchan not found")
    endif()
endif()

# ######################################################################################################################
# Target properties
# ######################################################################################################################

set(TARGET_NAMESPACE ${TARGET_NAMESPACE}::mp)
set(TARGET_PREFIX ${TARGET_PREFIX}_mp)

# ######################################################################################################################
# Compiler flags
# ######################################################################################################################

if(WITH_VCHAN)
    add_definitions(-DVCHAN)
endif()

# ######################################################################################################################
# Modules
# ######################################################################################################################

add_subdirectory(app)
add_subdirectory(cmclient)
add_subdirectory(communication)
add_subdirectory(config)
add_subdirectory(filechunker)
add_subdirectory(iamclient)
add_subdirectory(imageunpacker)
add_subdirectory(logprovider)
