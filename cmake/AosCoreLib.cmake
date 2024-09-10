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
    GIT_REPOSITORY https://github.com/aosedge/aos_core_lib_cpp.git
    GIT_TAG v1.0.0
    GIT_PROGRESS TRUE
    GIT_SHALLOW TRUE
    CMAKE_ARGS -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DCMAKE_INSTALL_PREFIX=${aoscore_build_dir}
               -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE} -DWITH_TEST=${WITH_TEST}
    UPDATE_COMMAND ""
)

file(MAKE_DIRECTORY ${aoscore_build_dir}/include)

include(AosCoreLibInclude)

add_dependencies(aoscommon aoscore)
add_dependencies(aosiam aoscore)
add_dependencies(mbedtls::crypto aoscore)
add_dependencies(mbedtls::mbedtls aoscore)
add_dependencies(mbedtls::mbedx509 aoscore)

if(WITH_TEST)
    add_dependencies(aoscoretestutils aoscore)
endif()
