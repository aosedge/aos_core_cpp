# Copyright (C) 2024 Renesas Electronics Corporation.
# Copyright (C) 2024 EPAM Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

find_package(PkgConfig QUIET)
pkg_check_modules(PC_XENVCHAN QUIET xenvchan)

find_path(
    XENVCHAN_INCLUDE_DIR
    NAMES libxenvchan.h
    PATHS ${PC_XENVCHAN_INCLUDE_DIRS}
    PATH_SUFFIXES xen
)

find_library(
    XENVCHAN_LIBRARY
    NAMES xenvchan
    PATHS ${PC_XENVCHAN_LIBRARY_DIRS}
)

set(XENVCHAN_VERSION ${PC_XENVCHAN_VERSION})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
    Xenvchan
    REQUIRED_VARS XENVCHAN_LIBRARY XENVCHAN_INCLUDE_DIR
    VERSION_VAR XENVCHAN_VERSION
)

if(XENVCHAN_FOUND)
    set(XENVCHAN_LIBRARIES ${XENVCHAN_LIBRARY})
    set(XENVCHAN_INCLUDE_DIRS ${XENVCHAN_INCLUDE_DIR})
    if(NOT TARGET Xenvchan::Xenvchan)
        add_library(Xenvchan::Xenvchan UNKNOWN IMPORTED)
        set_target_properties(
            Xenvchan::Xenvchan PROPERTIES IMPORTED_LOCATION "${XENVCHAN_LIBRARY}" INTERFACE_INCLUDE_DIRECTORIES
                                                                                  "${XENVCHAN_INCLUDE_DIR}"
        )
    endif()
endif()

mark_as_advanced(XENVCHAN_INCLUDE_DIR XENVCHAN_LIBRARY)
