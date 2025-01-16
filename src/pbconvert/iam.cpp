/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pbconvert/iam.hpp"
#include "pbconvert/common.hpp"

namespace aos::common::pbconvert {

iamanager::v5::Subjects ConvertToProto(const Array<StaticString<cSubjectIDLen>>& src)
{
    iamanager::v5::Subjects result;

    for (const auto& subject : src) {
        result.add_subjects(subject.CStr());
    }

    return result;
}

iamanager::v5::NodeAttribute ConvertToProto(const NodeAttribute& src)
{
    iamanager::v5::NodeAttribute result;

    result.set_name(src.mName.CStr());
    result.set_value(src.mValue.CStr());

    return result;
}

iamanager::v5::PartitionInfo ConvertToProto(const PartitionInfo& src)
{
    iamanager::v5::PartitionInfo result;

    result.set_name(src.mName.CStr());
    result.set_total_size(src.mTotalSize);
    result.set_path(src.mPath.CStr());

    for (const auto& type : src.mTypes) {
        result.add_types(type.CStr());
    }

    return result;
}

iamanager::v5::CPUInfo ConvertToProto(const CPUInfo& src)
{
    iamanager::v5::CPUInfo result;

    result.set_model_name(src.mModelName.CStr());
    result.set_num_cores(src.mNumCores);
    result.set_num_threads(src.mNumThreads);
    result.set_arch(src.mArch.CStr());
    result.set_arch_family(src.mArchFamily.CStr());

    return result;
}

iamanager::v5::NodeInfo ConvertToProto(const NodeInfo& src)
{
    iamanager::v5::NodeInfo result;

    result.set_node_id(src.mNodeID.CStr());
    result.set_node_type(src.mNodeType.CStr());
    result.set_name(src.mName.CStr());
    result.set_status(src.mStatus.ToString().CStr());
    result.set_os_type(src.mOSType.CStr());
    result.set_max_dmips(src.mMaxDMIPS);
    result.set_total_ram(src.mTotalRAM);

    for (const auto& attr : src.mAttrs) {
        *result.add_attrs() = ConvertToProto(attr);
    }

    for (const auto& partition : src.mPartitions) {
        *result.add_partitions() = ConvertToProto(partition);
    }

    for (const auto& cpuInfo : src.mCPUs) {
        *result.add_cpus() = ConvertToProto(cpuInfo);
    }

    return result;
}

RetWithError<std::string> ConvertSerialToProto(const StaticArray<uint8_t, crypto::cSerialNumSize>& src)
{
    StaticString<crypto::cSerialNumStrLen> result;

    auto err = result.ByteArrayToHex(src);

    return {result.Get(), err};
}

} // namespace aos::common::pbconvert
