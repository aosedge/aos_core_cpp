/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <cstring>

#include "common.hpp"
#include "iam.hpp"

namespace aos::common::pbconvert {

iamanager::v6::Subjects ConvertToProto(const Array<StaticString<cIDLen>>& src)
{
    iamanager::v6::Subjects result;

    for (const auto& subject : src) {
        result.add_subjects(subject.CStr());
    }

    return result;
}

iamanager::v6::NodeAttribute ConvertToProto(const NodeAttribute& src)
{
    iamanager::v6::NodeAttribute result;

    result.set_name(src.mName.CStr());
    result.set_value(src.mValue.CStr());

    return result;
}

iamanager::v6::PartitionInfo ConvertToProto(const PartitionInfo& src)
{
    iamanager::v6::PartitionInfo result;

    result.set_name(src.mName.CStr());
    result.set_total_size(src.mTotalSize);
    result.set_path(src.mPath.CStr());

    for (const auto& type : src.mTypes) {
        result.add_types(type.CStr());
    }

    return result;
}

iamanager::v6::CPUInfo ConvertToProto(const CPUInfo& src)
{
    iamanager::v6::CPUInfo result;

    result.set_model_name(src.mModelName.CStr());
    result.set_num_cores(src.mNumCores);
    result.set_num_threads(src.mNumThreads);

    auto* archInfo = result.mutable_arch_info();
    archInfo->set_architecture(src.mArchInfo.mArchitecture.CStr());

    if (src.mArchInfo.mVariant.HasValue()) {
        archInfo->set_variant(src.mArchInfo.mVariant->CStr());
    }

    if (src.mMaxDMIPS.HasValue()) {
        result.set_max_dmips(*src.mMaxDMIPS);
    }

    return result;
}

iamanager::v6::NodeInfo ConvertToProto(const NodeInfo& src)
{
    iamanager::v6::NodeInfo result;

    result.set_node_id(src.mNodeID.CStr());
    result.set_node_type(src.mNodeType.CStr());
    result.set_title(src.mTitle.CStr());
    result.set_max_dmips(src.mMaxDMIPS);
    result.set_total_ram(src.mTotalRAM);

    if (src.mPhysicalRAM.HasValue()) {
        result.set_physical_ram(*src.mPhysicalRAM);
    }

    ConvertOSInfoToProto(src.mOSInfo, *result.mutable_os_info());

    for (const auto& cpuInfo : src.mCPUs) {
        *result.add_cpus() = ConvertToProto(cpuInfo);
    }

    for (const auto& partition : src.mPartitions) {
        *result.add_partitions() = ConvertToProto(partition);
    }

    for (const auto& attr : src.mAttrs) {
        *result.add_attrs() = ConvertToProto(attr);
    }

    result.set_provisioned(src.mProvisioned);

    result.set_state(src.mState.ToString().CStr());

    if (!src.mError.IsNone()) {
        *result.mutable_error() = ConvertAosErrorToProto(src.mError);
    }

    return result;
}

RetWithError<std::string> ConvertSerialToProto(const StaticArray<uint8_t, crypto::cSerialNumSize>& src)
{
    StaticString<crypto::cSerialNumStrLen> result;

    auto err = result.ByteArrayToHex(src);

    return {result.Get(), err};
}

iamanager::v6::PermissionsRequest ConvertToProto(const String& secret, const String& funcServerID)
{
    iamanager::v6::PermissionsRequest result;

    result.set_secret(secret.CStr());
    result.set_functional_server_id(funcServerID.CStr());

    return result;
}

Error ConvertToAos(const iamanager::v6::PermissionsResponse& src, InstanceIdent& instanceIdent,
    Array<FunctionPermissions>& servicePermissions)
{
    instanceIdent = ConvertToAos(src.instance());

    for (const auto& [function, permissions] : src.permissions().permissions()) {
        FunctionPermissions funcPerm;

        funcPerm.mFunction    = function.c_str();
        funcPerm.mPermissions = permissions.c_str();

        if (auto err = servicePermissions.PushBack(funcPerm); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    return ErrorEnum::eNone;
}

Error ConvertToAos(const iamanager::v6::CertInfo& src, CertInfo& dst)
{
    dst.mCertType = src.type().c_str();
    dst.mCertURL  = src.cert_url().c_str();
    dst.mKeyURL   = src.key_url().c_str();

    if (auto err = dst.mSerial.Resize(src.serial().size()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = dst.mSerial.Assign(
            Array<uint8_t>(reinterpret_cast<const uint8_t*>(src.serial().data()), src.serial().size()));
        !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = dst.mIssuer.Resize(src.issuer().size()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return dst.mIssuer.Assign(
        Array<uint8_t>(reinterpret_cast<const uint8_t*>(src.issuer().data()), src.issuer().size()));
}

} // namespace aos::common::pbconvert
