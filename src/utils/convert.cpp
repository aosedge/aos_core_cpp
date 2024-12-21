/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "convert.hpp"

namespace utils {

const aos::Array<uint8_t> ConvertByteArrayToAos(const std::string& arr)
{
    return {reinterpret_cast<const uint8_t*>(arr.c_str()), arr.length()};
}

void ConvertToProto(const aos::Array<aos::StaticString<aos::cSubjectIDLen>>& src, iamanager::v5::Subjects& dst)
{
    dst.clear_subjects();

    for (const auto& subject : src) {
        dst.add_subjects(subject.CStr());
    }
}

void ConvertToProto(const aos::NodeAttribute& src, iamanager::v5::NodeAttribute& dst)
{
    dst.set_name(src.mName.CStr());
    dst.set_value(src.mValue.CStr());
}

void ConvertToProto(const aos::PartitionInfo& src, iamanager::v5::PartitionInfo& dst)
{
    dst.set_name(src.mName.CStr());
    dst.set_total_size(src.mTotalSize);
    dst.set_path(src.mPath.CStr());

    for (const auto& type : src.mTypes) {
        dst.add_types(type.CStr());
    }
}

void ConvertToProto(const aos::CPUInfo& src, iamanager::v5::CPUInfo& dst)
{
    dst.set_model_name(src.mModelName.CStr());
    dst.set_num_cores(src.mNumCores);
    dst.set_num_threads(src.mNumThreads);
    dst.set_arch(src.mArch.CStr());
    dst.set_arch_family(src.mArchFamily.CStr());
}

void ConvertToProto(const aos::NodeInfo& src, iamanager::v5::NodeInfo& dst)
{
    dst.set_node_id(src.mNodeID.CStr());
    dst.set_node_type(src.mNodeType.CStr());
    dst.set_name(src.mName.CStr());
    dst.set_status(src.mStatus.ToString().CStr());
    dst.set_os_type(src.mOSType.CStr());
    dst.set_max_dmips(src.mMaxDMIPS);
    dst.set_total_ram(src.mTotalRAM);

    for (const auto& attr : src.mAttrs) {
        ConvertToProto(attr, *dst.add_attrs());
    }

    for (const auto& partition : src.mPartitions) {
        ConvertToProto(partition, *dst.add_partitions());
    }

    for (const auto& cpuInfo : src.mCPUs) {
        ConvertToProto(cpuInfo, *dst.add_cpus());
    }
}

aos::RetWithError<std::string> ConvertSerialToProto(const aos::StaticArray<uint8_t, aos::crypto::cSerialNumSize>& src)
{
    aos::StaticString<aos::crypto::cSerialNumStrLen> result;

    auto err = result.ByteArrayToHex(src);

    return {result.Get(), err};
}

common::v1::ErrorInfo ConvertAosErrorToProto(const aos::Error& error)
{
    common::v1::ErrorInfo result;

    result.set_aos_code(static_cast<int32_t>(error.Value()));
    result.set_exit_code(error.Errno());

    if (!error.IsNone()) {
        aos::StaticString<aos::cErrorMessageLen> message;

        auto err = message.Convert(error);

        result.set_message(err.IsNone() ? message.CStr() : error.Message());
    }

    return result;
}

grpc::Status ConvertAosErrorToGrpcStatus(const aos::Error& error)
{
    if (error.IsNone()) {
        return grpc::Status::OK;
    }

    if (aos::StaticString<aos::cErrorMessageLen> message; message.Convert(error).IsNone()) {
        return grpc::Status(grpc::StatusCode::INTERNAL, message.CStr());
    }

    return grpc::Status(grpc::StatusCode::INTERNAL, error.Message());
}

} // namespace utils
