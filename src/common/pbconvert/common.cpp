/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "common.hpp"

namespace aos::common::pbconvert {

::common::v2::ErrorInfo ConvertAosErrorToProto(const Error& error)
{
    ::common::v2::ErrorInfo result;

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

::common::v2::InstanceIdent ConvertToProto(const InstanceIdent& src)
{
    ::common::v2::InstanceIdent result;

    result.set_item_id(src.mItemID.CStr());
    result.set_subject_id(src.mSubjectID.CStr());
    result.set_instance(src.mInstance);
    result.set_type(static_cast<::common::v2::ItemType>(src.mType.GetValue()));

    return result;
}

iamanager::v6::RegisterInstanceRequest ConvertToProto(
    const InstanceIdent& instanceIdent, const Array<FunctionServicePermissions>& instancePermissions)
{
    iamanager::v6::RegisterInstanceRequest result;

    result.mutable_instance()->CopyFrom(ConvertToProto(instanceIdent));

    for (const auto& servicePerm : instancePermissions) {
        auto& permissions = (*result.mutable_permissions())[servicePerm.mName.CStr()];

        for (const auto& perm : servicePerm.mPermissions) {
            (*permissions.mutable_permissions())[perm.mFunction.CStr()] = perm.mPermissions.CStr();
        }
    }

    return result;
}

InstanceIdent ConvertToAos(const ::common::v2::InstanceIdent& val)
{
    InstanceIdent result;

    result.mItemID    = val.item_id().c_str();
    result.mSubjectID = val.subject_id().c_str();
    result.mInstance  = val.instance();
    result.mType      = static_cast<UpdateItemTypeEnum>(val.type());

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

void ConvertToAos(const ::common::v2::InstanceFilter& src, InstanceFilter& dst)
{
    if (!src.item_id().empty()) {
        dst.mItemID.SetValue(src.item_id().c_str());
    }

    if (!src.subject_id().empty()) {
        dst.mSubjectID.SetValue(src.subject_id().c_str());
    }

    if (src.instance() >= 0) {
        dst.mInstance.SetValue(static_cast<uint64_t>(src.instance()));
    }
}

void ConvertOSInfoToProto(const OSInfo& src, iamanager::v6::OSInfo& dst)
{
    dst.set_os(src.mOS.CStr());

    if (src.mVersion.HasValue()) {
        dst.set_version(src.mVersion->CStr());
    }

    for (const auto& feature : src.mFeatures) {
        dst.add_features(feature.CStr());
    }
}

Error ConvertToAos(const google::protobuf::RepeatedPtrField<iamanager::v6::CPUInfo>& src, CPUInfoArray& dst)
{
    for (const auto& srcCPU : src) {
        CPUInfo dstCPU;

        dstCPU.mModelName              = srcCPU.model_name().c_str();
        dstCPU.mNumCores               = srcCPU.num_cores();
        dstCPU.mNumThreads             = srcCPU.num_threads();
        dstCPU.mArchInfo.mArchitecture = srcCPU.arch_info().architecture().c_str();

        if (!srcCPU.arch_info().variant().empty()) {
            dstCPU.mArchInfo.mVariant.SetValue(srcCPU.arch_info().variant().c_str());
        }

        if (srcCPU.max_dmips() > 0) {
            dstCPU.mMaxDMIPS.SetValue(srcCPU.max_dmips());
        }

        if (auto err = dst.PushBack(dstCPU); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    return ErrorEnum::eNone;
}

Error ConvertToAos(const google::protobuf::RepeatedPtrField<iamanager::v6::PartitionInfo>& src, PartitionInfoArray& dst)
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

Error ConvertToAos(const google::protobuf::RepeatedPtrField<iamanager::v6::NodeAttribute>& src, NodeAttributeArray& dst)
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

Error ConvertToAos(const iamanager::v6::NodeInfo& src, NodeInfo& dst)
{
    dst.mNodeID   = src.node_id().c_str();
    dst.mNodeType = src.node_type().c_str();
    dst.mTitle    = src.title().c_str();

    dst.mMaxDMIPS = src.max_dmips();
    dst.mTotalRAM = src.total_ram();

    if (src.physical_ram() > 0) {
        dst.mPhysicalRAM.SetValue(src.physical_ram());
    }

    dst.mOSInfo.mOS = src.os_info().os().c_str();
    if (!src.os_info().version().empty()) {
        dst.mOSInfo.mVersion.SetValue(src.os_info().version().c_str());
    }

    for (const auto& feature : src.os_info().features()) {
        if (auto err = dst.mOSInfo.mFeatures.PushBack(feature.c_str()); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    if (auto err = ConvertToAos(src.cpus(), dst.mCPUs); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = ConvertToAos(src.partitions(), dst.mPartitions); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = ConvertToAos(src.attrs(), dst.mAttrs); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = dst.mState.FromString(src.state().c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (src.has_error()) {
        dst.mError = Error(src.error().exit_code(), src.error().message().c_str());
    } else {
        dst.mError = ErrorEnum::eNone;
    }

    return ErrorEnum::eNone;
}

} // namespace aos::common::pbconvert
