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

aos::Error ConvertToAos(
    const google::protobuf::RepeatedPtrField<iamanager::v5::CPUInfo>& src, aos::CPUInfoStaticArray& dst)
{
    for (const auto& srcCPU : src) {
        aos::CPUInfo dstCPU;

        dstCPU.mModelName  = srcCPU.model_name().c_str();
        dstCPU.mNumCores   = srcCPU.num_cores();
        dstCPU.mNumThreads = srcCPU.num_threads();
        dstCPU.mArch       = srcCPU.arch().c_str();
        dstCPU.mArchFamily = srcCPU.arch_family().c_str();

        if (auto err = dst.PushBack(dstCPU); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    return aos::ErrorEnum::eNone;
}

aos::Error ConvertToAos(
    const google::protobuf::RepeatedPtrField<iamanager::v5::PartitionInfo>& src, aos::PartitionInfoStaticArray& dst)
{
    for (const auto& srcPartition : src) {
        aos::PartitionInfo dstPartition;

        dstPartition.mName      = srcPartition.name().c_str();
        dstPartition.mPath      = srcPartition.path().c_str();
        dstPartition.mTotalSize = srcPartition.total_size();

        for (const auto& srcType : srcPartition.types()) {
            if (auto err = dstPartition.mTypes.PushBack(srcType.c_str()); !err.IsNone()) {
                return AOS_ERROR_WRAP(err);
            }
        }

        if (auto err = dst.PushBack(dstPartition); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    return aos::ErrorEnum::eNone;
}

aos::Error ConvertToAos(
    const google::protobuf::RepeatedPtrField<iamanager::v5::NodeAttribute>& src, aos::NodeAttributeStaticArray& dst)
{
    for (const auto& srcAttribute : src) {
        aos::NodeAttribute dstAttribute;

        dstAttribute.mName  = srcAttribute.name().c_str();
        dstAttribute.mValue = srcAttribute.value().c_str();

        if (auto err = dst.PushBack(dstAttribute); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    return aos::ErrorEnum::eNone;
}

aos::Error ConvertToAos(const iamanager::v5::NodeInfo& src, aos::NodeInfo& dst)
{
    dst.mNodeID   = src.node_id().c_str();
    dst.mNodeType = src.node_type().c_str();
    dst.mName     = src.name().c_str();

    aos::NodeStatus nodeStatus;
    nodeStatus.FromString(src.status().c_str());

    dst.mStatus   = nodeStatus;
    dst.mOSType   = src.os_type().c_str();
    dst.mMaxDMIPS = src.max_dmips();
    dst.mTotalRAM = src.total_ram();

    if (auto err = ConvertToAos(src.cpus(), dst.mCPUs); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = ConvertToAos(src.partitions(), dst.mPartitions); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = ConvertToAos(src.attrs(), dst.mAttrs); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return aos::ErrorEnum::eNone;
}

aos::InstanceIdent ConvertToAos(const common::v1::InstanceIdent& val)
{
    aos::InstanceIdent result;

    result.mServiceID = val.service_id().c_str();
    result.mSubjectID = val.subject_id().c_str();
    result.mInstance  = val.instance();

    return result;
}

} // namespace utils
