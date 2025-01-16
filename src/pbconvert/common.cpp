/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pbconvert/common.hpp"

namespace aos::common::pbconvert {

::common::v1::ErrorInfo ConvertAosErrorToProto(const Error& error)
{
    ::common::v1::ErrorInfo result;

    result.set_aos_code(static_cast<int32_t>(error.Value()));
    result.set_exit_code(error.Errno());

    if (!error.IsNone()) {
        StaticString<cErrorMessageLen> message;

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

::common::v1::InstanceIdent ConvertToProto(const InstanceIdent& src)
{
    ::common::v1::InstanceIdent result;

    result.set_service_id(src.mServiceID.CStr());
    result.set_subject_id(src.mSubjectID.CStr());
    result.set_instance(src.mInstance);

    return result;
}

iamanager::v5::RegisterInstanceRequest ConvertToProto(
    const InstanceIdent& instanceIdent, const Array<FunctionServicePermissions>& instancePermissions)
{
    iamanager::v5::RegisterInstanceRequest result;

    result.mutable_instance()->CopyFrom(ConvertToProto(instanceIdent));

    for (const auto& servicePerm : instancePermissions) {
        auto& permissions = (*result.mutable_permissions())[servicePerm.mName.CStr()];

        for (const auto& perm : servicePerm.mPermissions) {
            (*permissions.mutable_permissions())[perm.mFunction.CStr()] = perm.mPermissions.CStr();
        }
    }

    return result;
}

InstanceIdent ConvertToAos(const ::common::v1::InstanceIdent& val)
{
    InstanceIdent result;

    result.mServiceID = val.service_id().c_str();
    result.mSubjectID = val.subject_id().c_str();
    result.mInstance  = val.instance();

    return result;
}

Optional<Time> ConvertToAos(const google::protobuf::Timestamp& val)
{
    Optional<Time> result;

    if (val.seconds() > 0) {
        result.SetValue(Time::Unix(val.seconds(), val.nanos()));
    }

    return result;
}

google::protobuf::Timestamp TimestampToPB(const aos::Time& time)
{
    auto unixTime = time.UnixTime();

    google::protobuf::Timestamp result;

    result.set_seconds(unixTime.tv_sec);
    result.set_nanos(static_cast<int32_t>(unixTime.tv_nsec));

    return result;
}

Error ConvertToAos(const google::protobuf::RepeatedPtrField<iamanager::v5::CPUInfo>& src, CPUInfoStaticArray& dst)
{
    for (const auto& srcCPU : src) {
        CPUInfo dstCPU;

        dstCPU.mModelName  = srcCPU.model_name().c_str();
        dstCPU.mNumCores   = srcCPU.num_cores();
        dstCPU.mNumThreads = srcCPU.num_threads();
        dstCPU.mArch       = srcCPU.arch().c_str();
        dstCPU.mArchFamily = srcCPU.arch_family().c_str();
        dstCPU.mMaxDMIPS   = srcCPU.max_dmips();

        if (auto err = dst.PushBack(dstCPU); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    return ErrorEnum::eNone;
}

Error ConvertToAos(
    const google::protobuf::RepeatedPtrField<iamanager::v5::PartitionInfo>& src, PartitionInfoStaticArray& dst)
{
    for (const auto& srcPartition : src) {
        PartitionInfo dstPartition;

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

    return ErrorEnum::eNone;
}

Error ConvertToAos(
    const google::protobuf::RepeatedPtrField<iamanager::v5::NodeAttribute>& src, NodeAttributeStaticArray& dst)
{
    for (const auto& srcAttribute : src) {
        NodeAttribute dstAttribute;

        dstAttribute.mName  = srcAttribute.name().c_str();
        dstAttribute.mValue = srcAttribute.value().c_str();

        if (auto err = dst.PushBack(dstAttribute); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    return ErrorEnum::eNone;
}

Error ConvertToAos(const iamanager::v5::NodeInfo& src, NodeInfo& dst)
{
    dst.mNodeID   = src.node_id().c_str();
    dst.mNodeType = src.node_type().c_str();
    dst.mName     = src.name().c_str();

    NodeStatus nodeStatus;
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

    return ErrorEnum::eNone;
}

} // namespace aos::common::pbconvert
