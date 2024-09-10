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
    GIT_REPOSITORY https://github.com/aosedge/aos_core_api.git
    GIT_TAG develop
    GIT_PROGRESS TRUE
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(aoscoreapi)

# ######################################################################################################################
# Set include directory for the AOS protocol headers
# ######################################################################################################################

set(AOSPROTOCOL_INCLUDE_DIR "${aoscoreapi_SOURCE_DIR}/aosprotocol")

add_library(aosprotocol INTERFACE)
target_include_directories(aosprotocol INTERFACE ${AOSPROTOCOL_INCLUDE_DIR})

# ######################################################################################################################
# Generate gRPC stubs
# ######################################################################################################################

find_package(gRPC REQUIRED)
find_package(Protobuf REQUIRED)

set(PROTO_DST_DIR "${CMAKE_CURRENT_BINARY_DIR}/aoscoreapi/gen")
set(PROTO_SRC_DIR "${aoscoreapi_SOURCE_DIR}/proto")
set(COMMON_PROTO_SRC_DIR "${aoscoreapi_SOURCE_DIR}/proto")

set(PROTO_COMMON_DST_DIR "${PROTO_DST_DIR}/common/v1")
set(PROTO_COMMON_SRC_DIR "${aoscoreapi_SOURCE_DIR}/proto/common/v1")

set(PROTO_SM_DST_DIR "${PROTO_DST_DIR}/servicemanager/v4")
set(PROTO_SM_SRC_DIR "${aoscoreapi_SOURCE_DIR}/proto/servicemanager/v4")

set(PROTO_IAM_DST_DIR "${PROTO_DST_DIR}/iamanager/v5")
set(PROTO_IAM_SRC_DIR "${aoscoreapi_SOURCE_DIR}/proto/iamanager/v5")

file(MAKE_DIRECTORY ${PROTO_DST_DIR})

if(CMAKE_CROSSCOMPILING)
    find_program(_GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin)
else()
    set(_GRPC_CPP_PLUGIN_EXECUTABLE $<TARGET_FILE:gRPC::grpc_cpp_plugin>)
endif()

message(STATUS "gRPC plugin: ${_GRPC_CPP_PLUGIN_EXECUTABLE}")

set(PROTO_FILES "${PROTO_SM_SRC_DIR}/servicemanager.proto" "${PROTO_COMMON_SRC_DIR}/common.proto"
                "${PROTO_IAM_SRC_DIR}/iamanager.proto"
)

set(PROTO_GENERATED_FILES
    "${PROTO_COMMON_DST_DIR}/common.pb.cc" "${PROTO_COMMON_DST_DIR}/common.pb.h"
    "${PROTO_SM_DST_DIR}/servicemanager.pb.cc" "${PROTO_SM_DST_DIR}/servicemanager.pb.h"
    "${PROTO_IAM_DST_DIR}/iamanager.pb.cc" "${PROTO_IAM_DST_DIR}/iamanager.pb.h"
)
set(GRPC_GENERATED_FILES
    "${PROTO_SM_DST_DIR}/servicemanager.grpc.pb.cc" "${PROTO_SM_DST_DIR}/servicemanager.grpc.pb.h"
    "${PROTO_SM_DST_DIR}/servicemanager_mock.grpc.pb.h" "${PROTO_IAM_DST_DIR}/iamanager.grpc.pb.cc"
    "${PROTO_IAM_DST_DIR}/iamanager.grpc.pb.h" "${PROTO_IAM_DST_DIR}/iamanager_mock.grpc.pb.h"
)

add_custom_command(
    OUTPUT ${PROTO_GENERATED_FILES}
    COMMAND ${Protobuf_PROTOC_EXECUTABLE} ARGS --cpp_out "${PROTO_DST_DIR}" -I ${PROTO_SRC_DIR} -I ${PROTO_SM_SRC_DIR}
            ${PROTO_FILES}
    DEPENDS ${PROTO_FILES}
)

add_custom_command(
    OUTPUT ${GRPC_GENERATED_FILES}
    COMMAND
        ${Protobuf_PROTOC_EXECUTABLE} ARGS --grpc_out=generate_mock_code=true:"${PROTO_DST_DIR}"
        --plugin=protoc-gen-grpc=${_GRPC_CPP_PLUGIN_EXECUTABLE} -I ${PROTO_SRC_DIR} -I
        ${PROTO_SRC_DIR}/servicemanager/v4 ${PROTO_FILES}
    DEPENDS ${PROTO_FILES} ${PROTO_GENERATED_FILES}
)

add_library(aoscoreapi-gen-objects OBJECT ${PROTO_GENERATED_FILES} ${GRPC_GENERATED_FILES})

target_link_libraries(aoscoreapi-gen-objects PUBLIC protobuf::libprotobuf gRPC::grpc++)
target_include_directories(aoscoreapi-gen-objects PUBLIC "${PROTO_DST_DIR}")
