# # Copyright (C) 2024 Renesas Electronics Corporation.
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
find_package(Protobuf REQUIRED)

set(PROTO_DST_DIR "${CMAKE_CURRENT_BINARY_DIR}/gen")
set(PROTO_SRC_DIR "${aoscoreapi_SOURCE_DIR}/proto/iamanager/v4")

message(STATUS "proto source dir: ${PROTO_SRC_DIR}")

file(MAKE_DIRECTORY ${PROTO_DST_DIR})

if(CMAKE_CROSSCOMPILING)
    find_program(_GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin)
else()
    set(_GRPC_CPP_PLUGIN_EXECUTABLE $<TARGET_FILE:gRPC::grpc_cpp_plugin>)
endif()

message(STATUS "gRPC plugin: ${_GRPC_CPP_PLUGIN_EXECUTABLE}")

set(PROTO_FILES "${PROTO_SRC_DIR}/iamanager.proto")

add_custom_command(
    OUTPUT "${PROTO_DST_DIR}/iamanager.pb.cc" "${PROTO_DST_DIR}/iamanager.pb.h"
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
    ARGS --cpp_out "${PROTO_DST_DIR}"
         -I ${PROTO_SRC_DIR}
         ${PROTO_FILES}
    DEPENDS ${PROTO_FILES}
)

add_custom_command(
  OUTPUT "${PROTO_DST_DIR}/iamanager.grpc.pb.cc"
         "${PROTO_DST_DIR}/iamanager.grpc.pb.h"
         "${PROTO_DST_DIR}/iamanager_mock.grpc.pb.h"
  COMMAND ${Protobuf_PROTOC_EXECUTABLE}
  ARGS --grpc_out=generate_mock_code=true:"${PROTO_DST_DIR}"
       --plugin=protoc-gen-grpc=${_GRPC_CPP_PLUGIN_EXECUTABLE}
       -I ${PROTO_SRC_DIR}
       ${PROTO_FILES}
  DEPENDS ${PROTO_FILES} "${PROTO_DST_DIR}/iamanager.pb.cc" "${PROTO_DST_DIR}/iamanager.pb.h"
)

add_library(aoscoreapi-gen-objects OBJECT
    "${PROTO_DST_DIR}/iamanager.pb.cc"
    "${PROTO_DST_DIR}/iamanager.grpc.pb.cc"
)

target_link_libraries(aoscoreapi-gen-objects PUBLIC protobuf::libprotobuf gRPC::grpc++)
target_include_directories(aoscoreapi-gen-objects PUBLIC "${PROTO_DST_DIR}")

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
add_subdirectory(client)
