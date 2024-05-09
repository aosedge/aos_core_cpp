#
# Copyright (C) 2024 Renesas Electronics Corporation.
# Copyright (C) 2024 EPAM Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

include(ExternalProject)

set(aoscorecommon_build_dir ${CMAKE_CURRENT_BINARY_DIR}/aoscorecommon)

ExternalProject_Add(
    aoscorecommon
    PREFIX ${aoscorecommon_build_dir}
    GIT_REPOSITORY https://github.com/aoscloud/aos_core_common_cpp.git
    GIT_TAG develop
    GIT_PROGRESS TRUE
    GIT_SHALLOW TRUE
    CMAKE_ARGS -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DCMAKE_INSTALL_PREFIX=${aoscorecommon_build_dir}
               -DWITH_TEST=${WITH_TEST} -DCMAKE_MODULE_PATH=${CMAKE_BINARY_DIR} -DCMAKE_PREFIX_PATH=${CMAKE_BINARY_DIR}
    UPDATE_COMMAND ""
)

file(MAKE_DIRECTORY ${aoscorecommon_build_dir}/include)

find_package(PkgConfig REQUIRED)
pkg_check_modules(JOURNALD libsystemd REQUIRED)

add_library(aosmigration STATIC IMPORTED GLOBAL)
set_target_properties(aosmigration PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${aoscorecommon_build_dir}/include)
set_target_properties(aosmigration PROPERTIES IMPORTED_LOCATION ${aoscorecommon_build_dir}/lib/libmigration.a)
add_dependencies(aosmigration aoscorecommon)

add_library(aosutils STATIC IMPORTED GLOBAL)
set_target_properties(aosutils PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${aoscorecommon_build_dir}/include)
set_target_properties(aosutils PROPERTIES IMPORTED_LOCATION ${aoscorecommon_build_dir}/lib/libutils.a)
target_link_libraries(aosutils INTERFACE gRPC::grpc++ aoscommon)
add_dependencies(aosutils aoscorecommon)

add_library(aoslogger STATIC IMPORTED GLOBAL)
set_target_properties(aoslogger PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${aoscorecommon_build_dir}/include)
set_target_properties(aoslogger PROPERTIES IMPORTED_LOCATION ${aoscorecommon_build_dir}/lib/liblogger.a)
target_link_libraries(aoslogger INTERFACE ${JOURNALD_LIBRARIES})
add_dependencies(aoslogger aoscorecommon)
