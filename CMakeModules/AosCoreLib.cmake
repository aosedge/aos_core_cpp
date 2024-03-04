#
# Copyright (C) 2024 Renesas Electronics Corporation.
# Copyright (C) 2024 EPAM Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

include(ExternalProject)

set(aoscore_build_dir ${CMAKE_CURRENT_BINARY_DIR}/aoscore)

ExternalProject_Add(
    aoscore
    PREFIX ${aoscore_build_dir}
    GIT_REPOSITORY https://github.com/aoscloud/aos_core_lib_cpp.git
    GIT_TAG main
    GIT_PROGRESS TRUE
    GIT_SHALLOW TRUE
    CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${aoscore_build_dir}
    UPDATE_COMMAND ""
)

file(MAKE_DIRECTORY ${aoscore_build_dir}/include)

add_library(aoscommon STATIC IMPORTED GLOBAL)
set_target_properties(aoscommon PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${aoscore_build_dir}/include)
set_target_properties(aoscommon PROPERTIES IMPORTED_LOCATION ${aoscore_build_dir}/lib/libaoscommoncpp.a)

add_library(aosiam STATIC IMPORTED GLOBAL)
set_target_properties(aosiam PROPERTIES IMPORTED_LOCATION ${aoscore_build_dir}/lib/libaosiamcpp.a)
