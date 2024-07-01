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
    GIT_TAG develop
    GIT_PROGRESS TRUE
    GIT_SHALLOW TRUE
    CMAKE_ARGS -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DCMAKE_INSTALL_PREFIX=${aoscore_build_dir}
               -DWITH_TEST=${WITH_TEST}
    UPDATE_COMMAND ""
)

file(MAKE_DIRECTORY ${aoscore_build_dir}/include)

add_library(aoscommon STATIC IMPORTED GLOBAL)
target_include_directories(aoscommon SYSTEM INTERFACE ${aoscore_build_dir}/include)
set_target_properties(aoscommon PROPERTIES IMPORTED_LOCATION ${aoscore_build_dir}/lib/libaoscommoncpp.a)
add_dependencies(aoscommon aoscore)

add_library(aosiam STATIC IMPORTED GLOBAL)
set_target_properties(aosiam PROPERTIES IMPORTED_LOCATION ${aoscore_build_dir}/lib/libaosiamcpp.a)
add_dependencies(aosiam aoscore)

add_library(mbedtls::crypto STATIC IMPORTED GLOBAL)
set_target_properties(mbedtls::crypto PROPERTIES IMPORTED_LOCATION ${aoscore_build_dir}/lib/libmbedcrypto.a)
add_dependencies(mbedtls::crypto aoscore)

add_library(mbedtls::mbedtls STATIC IMPORTED GLOBAL)
set_target_properties(mbedtls::mbedtls PROPERTIES IMPORTED_LOCATION ${aoscore_build_dir}/lib/libmbedtls.a)
add_dependencies(mbedtls::mbedtls aoscore)

add_library(mbedtls::mbedx509 STATIC IMPORTED GLOBAL)
set_target_properties(mbedtls::mbedx509 PROPERTIES IMPORTED_LOCATION ${aoscore_build_dir}/lib/libmbedx509.a)
add_dependencies(mbedtls::mbedx509 aoscore)

add_library(mbedtls INTERFACE IMPORTED)
set_property(TARGET mbedtls PROPERTY INTERFACE_LINK_LIBRARIES mbedtls::mbedx509 mbedtls::mbedtls mbedtls::crypto)

if(WITH_TEST)
    add_library(aoscoretestutils STATIC IMPORTED GLOBAL)
    set_target_properties(aoscoretestutils PROPERTIES IMPORTED_LOCATION ${aoscore_build_dir}/lib/libtestutils.a)
    add_dependencies(aoscoretestutils aoscore)
endif()
