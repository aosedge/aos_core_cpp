/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CONVERT_HPP_
#define CONVERT_HPP_

#include <aos/common/crypto.hpp>
#include <aos/common/types.hpp>

#include <iamanager/v5/iamanager.grpc.pb.h>

namespace utils {

/**
 * Converts byte array to string.
 *
 * @param arr array to convert.
 * @return const aos::Array<uint8_t>.
 */
const aos::Array<uint8_t> ConvertByteArrayToAos(const std::string& arr);

/**
 * Converts aos string array to protobuf repeated string.
 *
 * @param src string to convert.
 * @param[out] dst destination repeated string.
 * @return void.
 */
template <size_t Size>
static void ConvertToProto(
    const aos::Array<aos::StaticString<Size>>& src, google::protobuf::RepeatedPtrField<std::string>& dst)
{
    for (const auto& val : src) {
        dst.Add(val.CStr());
    }
}

/**
 * Converts aos subjects array to protobuf subjects.
 *
 * @param src aos subjects.
 * @param[out] dst destination protobuf subjects.
 * @return void.
 */
void ConvertToProto(const aos::Array<aos::StaticString<aos::cSubjectIDLen>>& src, iamanager::v5::Subjects& dst);

/**
 * Converts aos node attribute to protobuf node attribute.
 *
 * @param src aos node attribute.
 * @param[out] dst destination protobuf node attribute.
 * @return void.
 */
void ConvertToProto(const aos::NodeAttribute& src, iamanager::v5::NodeAttribute& dst);

/**
 * Converts aos partition info to protobuf partition info.
 *
 * @param src aos partition info.
 * @param[out] dst destination protobuf partition info.
 * @return void.
 */
void ConvertToProto(const aos::PartitionInfo& src, iamanager::v5::PartitionInfo& dst);

/**
 * Converts aos cpu info to protobuf cpu info.
 *
 * @param src aos cpu info.
 * @param[out] dst destination protobuf cpu info.
 * @return void.
 */
void ConvertToProto(const aos::CPUInfo& src, iamanager::v5::CPUInfo& dst);

/**
 * Converts aos node info to protobuf node info.
 *
 * @param src aos node info.
 * @param[out] dst destination protobuf node info.
 * @return void.
 */
void ConvertToProto(const aos::NodeInfo& src, iamanager::v5::NodeInfo& dst);

/**
 * Converts aos serial number to protobuf.
 *
 * @param src aos serial.
 * @return aos::RetWithError<std::string>.
 */
aos::RetWithError<std::string> ConvertSerialToProto(const aos::StaticArray<uint8_t, aos::crypto::cSerialNumSize>& src);

/**
 * Converts aos error to protobuf error.
 *
 * @param error aos error.
 * @return iamanager::v5::ErrorInfo.
 */
common::v1::ErrorInfo ConvertAosErrorToProto(const aos::Error& error);

/**
 * Converts aos error to grpc status.
 *
 * @param error aos error.
 * @return grpc::Status.
 */
grpc::Status ConvertAosErrorToGrpcStatus(const aos::Error& error);

/**
 * Converts protobuf cpus to aos.
 *
 * @param src protobuf cpus.
 * @param[out] dst aos cpus.
 * @return aos::Error.
 */
aos::Error ConvertToAos(
    const google::protobuf::RepeatedPtrField<iamanager::v5::CPUInfo>& src, aos::CPUInfoStaticArray& dst);

/**
 * Converts protobuf partitions to aos.
 *
 * @param src protobuf partitions.
 * @param[out] dst aos partitions.
 * @return aos::Error.
 */
aos::Error ConvertToAos(
    const google::protobuf::RepeatedPtrField<iamanager::v5::PartitionInfo>& src, aos::PartitionInfoStaticArray& dst);

/**
 * Converts protobuf node attributes to aos.
 *
 * @param src protobuf node attributes.
 * @param[out] dst aos node attributes.
 * @return aos::Error.
 */
aos::Error ConvertToAos(
    const google::protobuf::RepeatedPtrField<iamanager::v5::NodeAttribute>& src, aos::NodeAttributeStaticArray& dst);

/**
 * Converts protobuf node info to aos.
 *
 * @param src protobuf node info.
 * @param[out] dst aos node info.
 * @return aos::Error.
 */
aos::Error ConvertToAos(const iamanager::v5::NodeInfo& src, aos::NodeInfo& dst);

/**
 * Converts protobuf instance ident to aos.
 *
 * @param val protobuf instance ident.
 * @return aos::InstanceIdent.
 */
aos::InstanceIdent ConvertToAos(const common::v1::InstanceIdent& val);

/**
 * Sets protobuf error message from aos.
 *
 * @param src aos error.
 * @param[out] dst protobuf message.
 * @return void.
 */
template <typename Message>
void SetErrorInfo(const aos::Error& src, Message& dst)
{
    *dst.mutable_error() = ConvertAosErrorToProto(src);
}

} // namespace utils

#endif
