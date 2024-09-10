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
    GIT_REPOSITORY https://github.com/aosedge/aos_core_api.git
    GIT_TAG v8.0.0
    GIT_PROGRESS TRUE
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(aoscoreapi)

# ######################################################################################################################
# Generate gRPC stubs
# ######################################################################################################################

find_package(gRPC REQUIRED)
find_package(Protobuf REQUIRED)

set(PROTO_DST_DIR "${CMAKE_CURRENT_BINARY_DIR}/aoscoreapi/gen")
set(PROTO_SRC_DIR "${aoscoreapi_SOURCE_DIR}/proto")

file(MAKE_DIRECTORY ${PROTO_DST_DIR})

if(CMAKE_CROSSCOMPILING)
    find_program(_GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin)
else()
    set(_GRPC_CPP_PLUGIN_EXECUTABLE $<TARGET_FILE:gRPC::grpc_cpp_plugin>)
endif()

message(STATUS "gRPC plugin: ${_GRPC_CPP_PLUGIN_EXECUTABLE}")

set(PROTO_FILES ${PROTO_SRC_DIR}/common/v1/common.proto ${PROTO_SRC_DIR}/iamanager/v5/iamanager.proto
                ${PROTO_SRC_DIR}/iamanager/version.proto
)
set(PB_FILES
    ${PROTO_DST_DIR}/common/v1/common.pb.h ${PROTO_DST_DIR}/common/v1/common.pb.cc
    ${PROTO_DST_DIR}/iamanager/v5/iamanager.pb.h ${PROTO_DST_DIR}/iamanager/v5/iamanager.pb.cc
    ${PROTO_DST_DIR}/iamanager/version.pb.h ${PROTO_DST_DIR}/iamanager/version.pb.cc
)
set(GRPC_FILES ${PROTO_DST_DIR}/iamanager/v5/iamanager.grpc.pb.h ${PROTO_DST_DIR}/iamanager/v5/iamanager.grpc.pb.cc
               ${PROTO_DST_DIR}/iamanager/version.grpc.pb.h ${PROTO_DST_DIR}/iamanager/version.grpc.pb.cc
)
set(GRPC_MOCKS "${PROTO_DST_DIR}/iamanager/v5/iamanager_mock.grpc.pb.h")

add_custom_command(
    OUTPUT ${PB_FILES}
    COMMAND ${Protobuf_PROTOC_EXECUTABLE} ARGS --cpp_out "${PROTO_DST_DIR}" -I ${PROTO_SRC_DIR} ${PROTO_FILES}
    DEPENDS ${PROTO_FILES}
)

add_custom_command(
    OUTPUT ${GRPC_FILES} ${GRPC_MOCKS}
    COMMAND ${Protobuf_PROTOC_EXECUTABLE} ARGS --grpc_out=generate_mock_code=true:"${PROTO_DST_DIR}"
            --plugin=protoc-gen-grpc=${_GRPC_CPP_PLUGIN_EXECUTABLE} -I ${PROTO_SRC_DIR} ${PROTO_FILES}
    DEPENDS ${PROTO_FILES} ${PB_FILES}
)

add_library(aoscoreapi-gen-objects STATIC ${GRPC_FILES} ${GRPC_MOCKS} ${PB_FILES})

target_link_libraries(aoscoreapi-gen-objects PUBLIC protobuf::libprotobuf gRPC::grpc++)
target_include_directories(aoscoreapi-gen-objects PUBLIC "${PROTO_DST_DIR}")
