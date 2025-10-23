/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_PBCONVERT_PBCONVERT_HPP_
#define AOS_COMMON_PBCONVERT_PBCONVERT_HPP_

#include <google/protobuf/timestamp.pb.h>

#include <common/v2/common.grpc.pb.h>
#include <core/common/tools/optional.hpp>
#include <core/common/types/common.hpp>
#include <core/iam/permhandler/permhandler.hpp>
#include <iamanager/v6/iamanager.grpc.pb.h>

namespace aos::common::pbconvert {

/**
 * Converts aos error to protobuf error.
 *
 * @param error aos error.
 * @return iamanager::v2::ErrorInfo.
 */
::common::v2::ErrorInfo ConvertAosErrorToProto(const Error& error);

/**
 * Converts aos error to grpc status.
 *
 * @param error aos error.
 * @return grpc::Status.
 */
grpc::Status ConvertAosErrorToGrpcStatus(const aos::Error& error);

/**
 * Converts aos instance ident to protobuf.
 *
 * @param src instance ident to convert.
 * @param[out] dst protobuf instance ident.
 * @return ::common::v2::InstanceIdent.
 */
::common::v2::InstanceIdent ConvertToProto(const InstanceIdent& src);

/**
 * Converts aos instance permissions to protobuf.
 *
 * @param instanceIdent instance ident.
 * @param instancePermissions instance permissions to convert.
 * @return iamanager::v6::RegisterInstanceRequest.
 */
iamanager::v6::RegisterInstanceRequest ConvertToProto(
    const InstanceIdent& instanceIdent, const Array<FunctionServicePermissions>& instancePermissions);

/**
 * Converts protobuf instance ident to aos.
 *
 * @param val protobuf instance ident.
 * @return InstanceIdent.
 */
InstanceIdent ConvertToAos(const ::common::v2::InstanceIdent& val);

/**
 * Converts protobuf timestamp to aos.
 *
 * @param val protobuf timestamp.
 * @return Optional<Time>.
 */
Optional<Time> ConvertToAos(const google::protobuf::Timestamp& val);

/**
 * Converts aos time to protobuf timestamp.
 *
 * @param time aos time.
 * @return google::protobuf::Timestamp .
 */
google::protobuf::Timestamp TimestampToPB(const aos::Time& time);

/**
 * Converts aos OSInfo to protobuf OSInfo.
 *
 * @param src aos OSInfo.
 * @param[out] dst protobuf OSInfo.
 */
void ConvertOSInfoToProto(const OSInfo& src, iamanager::v6::OSInfo& dst);

/**
 * Converts protobuf cpus to aos.
 *
 * @param src protobuf cpus.
 * @param[out] dst aos cpus.
 * @return Error.
 */
Error ConvertToAos(const google::protobuf::RepeatedPtrField<iamanager::v6::CPUInfo>& src, CPUInfoArray& dst);

/**
 * Converts protobuf partitions to aos.
 *
 * @param src protobuf partitions.
 * @param[out] dst aos partitions.
 * @return Error.
 */
Error ConvertToAos(
    const google::protobuf::RepeatedPtrField<iamanager::v6::PartitionInfo>& src, PartitionInfoArray& dst);

/**
 * Converts protobuf node attributes to aos.
 *
 * @param src protobuf node attributes.
 * @param[out] dst aos node attributes.
 * @return Error.
 */
Error ConvertToAos(
    const google::protobuf::RepeatedPtrField<iamanager::v6::NodeAttribute>& src, NodeAttributeArray& dst);

/**
 * Converts protobuf node info to aos.
 *
 * @param src protobuf node info.
 * @param[out] dst aos node info.
 * @return Error.
 */
Error ConvertToAos(const iamanager::v6::NodeInfo& src, NodeInfo& dst);

/**
 * Sets protobuf error message from aos.
 *
 * @param src aos error.
 * @param[out] dst protobuf message.
 * @return void.
 */
template <typename Message>
void SetErrorInfo(const Error& src, Message& dst)
{
    if (!src.IsNone()) {
        *dst.mutable_error() = ConvertAosErrorToProto(src);
    } else {
        dst.clear_error();
    }
}

} // namespace aos::common::pbconvert

#endif
