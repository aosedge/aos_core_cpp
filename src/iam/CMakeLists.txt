#
# Copyright (C) 2024 Renesas Electronics Corporation.
# Copyright (C) 2024 EPAM Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

# ######################################################################################################################
# Add API repo
# ######################################################################################################################

include(FetchContent)

FetchContent_Declare(
    aoscoreapi
    GIT_REPOSITORY https://github.com/aoscloud/aos_core_api.git
    GIT_TAG develop
    GIT_PROGRESS TRUE
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(aoscoreapi)

# ######################################################################################################################
# Generate gRPC stubs
# ######################################################################################################################

find_package(gRPC REQUIRED)

set(PROTO_DST_DIR "${CMAKE_CURRENT_BINARY_DIR}/gen")
set(PROTO_SRC_DIR "${aoscoreapi_SOURCE_DIR}/proto/iamanager/v4")

add_library(aoscoreapi-gen-objects OBJECT "${PROTO_SRC_DIR}/iamanager.proto")

target_link_libraries(aoscoreapi-gen-objects PUBLIC protobuf::libprotobuf gRPC::grpc++)

target_include_directories(aoscoreapi-gen-objects PUBLIC "${PROTO_DST_DIR}")
file(MAKE_DIRECTORY ${PROTO_DST_DIR})

protobuf_generate(TARGET aoscoreapi-gen-objects IMPORT_DIRS ${PROTO_SRC_DIR} PROTOC_OUT_DIR "${PROTO_DST_DIR}")

protobuf_generate(
    TARGET
    aoscoreapi-gen-objects
    LANGUAGE
    grpc
    GENERATE_EXTENSIONS
    .grpc.pb.h
    .grpc.pb.cc
    PLUGIN
    "protoc-gen-grpc=\$<TARGET_FILE:gRPC::grpc_cpp_plugin>"
    IMPORT_DIRS
    ${PROTO_SRC_DIR}
    PROTOC_OUT_DIR
    "${PROTO_DST_DIR}"
)

# ######################################################################################################################
# IAM Common part
# ######################################################################################################################

add_library(iam_common STATIC grpchelper.cpp)

target_link_libraries(iam_common PUBLIC logger aoscommon aosiam aoscoreapi-gen-objects Poco::Util)

# ######################################################################################################################
# Include subdirectories
# ######################################################################################################################

include_directories(.)

add_subdirectory(server)
