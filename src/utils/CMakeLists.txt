#
# Copyright (C) 2024 Renesas Electronics Corporation.
# Copyright (C) 2024 EPAM Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

set(TARGET aosutils)

# ######################################################################################################################
# Sources
# ######################################################################################################################

set(SOURCES
    cleanupmanager.cpp
    cryptohelper.cpp
    exception.cpp
    filesystem.cpp
    grpchelper.cpp
    image.cpp
    json.cpp
    parser.cpp
    pkcs11helper.cpp
    retry.cpp
    time.cpp
)

# ######################################################################################################################
# Target
# ######################################################################################################################

add_library(${TARGET} STATIC ${SOURCES})

# ######################################################################################################################
# Includes
# ######################################################################################################################

target_include_directories(${TARGET} PUBLIC ${AOS_CORE_COMMON_LIB_DIR}/include)

# ######################################################################################################################
# Compiler flags
# ######################################################################################################################

target_compile_options(${TARGET} PRIVATE -Wstack-usage=${AOS_STACK_USAGE})

# ######################################################################################################################
# Libraries
# ######################################################################################################################

find_package(gRPC REQUIRED)

target_link_libraries(
    ${TARGET}
    PUBLIC aoscommon Poco::Foundation gRPC::grpc++ aoscoreapi-gen-iam
    PRIVATE Poco::JSON
)

# ######################################################################################################################
# Install
# ######################################################################################################################

install(TARGETS ${TARGET} DESTINATION lib)

install(
    DIRECTORY ${AOS_CORE_COMMON_LIB_DIR}/include/utils
    DESTINATION include
    FILES_MATCHING
    PATTERN "*.hpp"
)
