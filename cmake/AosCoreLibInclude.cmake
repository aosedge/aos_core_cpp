add_library(aoscommon STATIC IMPORTED GLOBAL)
target_include_directories(aoscommon SYSTEM INTERFACE ${aoscore_build_dir}/include)
set_target_properties(aoscommon PROPERTIES IMPORTED_LOCATION ${aoscore_build_dir}/lib/libaoscommoncpp.a)

add_library(aosiam STATIC IMPORTED GLOBAL)
set_target_properties(aosiam PROPERTIES IMPORTED_LOCATION ${aoscore_build_dir}/lib/libaosiamcpp.a)

add_library(mbedtls::crypto STATIC IMPORTED GLOBAL)
set_target_properties(mbedtls::crypto PROPERTIES IMPORTED_LOCATION ${aoscore_build_dir}/lib/libmbedcrypto.a)

add_library(mbedtls::mbedtls STATIC IMPORTED GLOBAL)
set_target_properties(mbedtls::mbedtls PROPERTIES IMPORTED_LOCATION ${aoscore_build_dir}/lib/libmbedtls.a)

add_library(mbedtls::mbedx509 STATIC IMPORTED GLOBAL)
set_target_properties(mbedtls::mbedx509 PROPERTIES IMPORTED_LOCATION ${aoscore_build_dir}/lib/libmbedx509.a)

add_library(mbedtls INTERFACE IMPORTED)
set_property(TARGET mbedtls PROPERTY INTERFACE_LINK_LIBRARIES mbedtls::mbedx509 mbedtls::mbedtls mbedtls::crypto)

if(WITH_TEST)
    add_library(aoscoretestutils STATIC IMPORTED GLOBAL)
    set_target_properties(aoscoretestutils PROPERTIES IMPORTED_LOCATION ${aoscore_build_dir}/lib/libtestutils.a)
endif()
