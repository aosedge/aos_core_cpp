/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gmock/gmock.h>

#include <aos/test/log.hpp>

#include "pbconvert/iam.hpp"

using namespace testing;

namespace aos::common::pbconvert {

namespace {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

PartitionInfo CreatePartitionInfo(const std::string& name)
{
    const StaticString<cPartitionTypeLen> types[] = {"type-1", "type-2"};

    PartitionInfo result;

    result.mName      = name.c_str();
    result.mTotalSize = 1024;
    result.mPath      = "path";
    result.mTypes     = Array<StaticString<cPartitionTypeLen>>(types, std::size(types));

    return result;
}

CPUInfo CreateCPUInfo(const std::string& modelName)
{
    CPUInfo result;

    result.mModelName  = modelName.c_str();
    result.mNumCores   = 4;
    result.mNumThreads = 8;
    result.mArch       = "arch";
    result.mArchFamily = "arch-family";

    return result;
}

NodeInfo CreateNodeInfo()
{
    NodeInfo result;

    result.mNodeID   = "node-id";
    result.mNodeType = "node-type";
    result.mName     = "name";
    result.mStatus   = NodeStatusEnum::eProvisioned;
    result.mOSType   = "os-type";
    result.mMaxDMIPS = 1024;
    result.mTotalRAM = 2048;

    result.mAttrs.PushBack(NodeAttribute {"attr-1", "value-1"});
    result.mAttrs.PushBack(NodeAttribute {"attr-2", "value-2"});

    result.mPartitions.PushBack(CreatePartitionInfo("partition-1"));
    result.mPartitions.PushBack(CreatePartitionInfo("partition-2"));

    result.mCPUs.PushBack(CreateCPUInfo("cpu-1"));
    result.mCPUs.PushBack(CreateCPUInfo("cpu-2"));

    return result;
}

} // namespace

class PBConvertIAMTest : public Test {
public:
    void SetUp() override { test::InitLog(); }
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(PBConvertIAMTest, ConvertSubjectsToProto)
{
    const StaticString<cSubjectIDLen> subjects[] = {"subject-id-1", "subject-id-2"};

    iamanager::v5::Subjects result = ConvertToProto(Array<StaticString<cSubjectIDLen>>(subjects, std::size(subjects)));

    ASSERT_EQ(result.subjects_size(), std::size(subjects));

    for (size_t i = 0; i < std::size(subjects); ++i) {
        EXPECT_STREQ(result.subjects(i).c_str(), subjects[i].CStr());
    }
}

TEST_F(PBConvertIAMTest, ConvertNodeAttributeToProto)
{
    NodeAttribute src;
    src.mName  = "name";
    src.mValue = "value";

    iamanager::v5::NodeAttribute result = ConvertToProto(src);

    EXPECT_STREQ(result.name().c_str(), src.mName.CStr());
    EXPECT_STREQ(result.value().c_str(), src.mValue.CStr());
}

TEST_F(PBConvertIAMTest, ConvertPartitionInfoToProto)
{
    const auto src    = CreatePartitionInfo("partition-name");
    const auto result = ConvertToProto(src);

    EXPECT_STREQ(result.name().c_str(), src.mName.CStr());
    EXPECT_EQ(result.total_size(), src.mTotalSize);
    EXPECT_STREQ(result.path().c_str(), src.mPath.CStr());

    ASSERT_EQ(result.types_size(), src.mTypes.Size());

    for (size_t i = 0; i < src.mTypes.Size(); ++i) {
        EXPECT_STREQ(result.types(i).c_str(), src.mTypes[i].CStr());
    }
}

TEST_F(PBConvertIAMTest, ConvertCPUInfoToProto)
{
    const auto src    = CreateCPUInfo("model-name");
    const auto result = ConvertToProto(src);

    EXPECT_STREQ(result.model_name().c_str(), src.mModelName.CStr());
    EXPECT_EQ(result.num_cores(), src.mNumCores);
    EXPECT_EQ(result.num_threads(), src.mNumThreads);
    EXPECT_STREQ(result.arch().c_str(), src.mArch.CStr());
    EXPECT_STREQ(result.arch_family().c_str(), src.mArchFamily.CStr());
}

TEST_F(PBConvertIAMTest, ConvertNodeInfoToProto)
{
    const auto src    = CreateNodeInfo();
    const auto result = ConvertToProto(src);

    EXPECT_STREQ(result.node_id().c_str(), src.mNodeID.CStr());
    EXPECT_STREQ(result.node_type().c_str(), src.mNodeType.CStr());
    EXPECT_STREQ(result.name().c_str(), src.mName.CStr());
    EXPECT_STREQ(result.status().c_str(), src.mStatus.ToString().CStr());
    EXPECT_STREQ(result.os_type().c_str(), src.mOSType.CStr());
    EXPECT_EQ(result.max_dmips(), src.mMaxDMIPS);
    EXPECT_EQ(result.total_ram(), src.mTotalRAM);

    ASSERT_EQ(result.attrs_size(), src.mAttrs.Size());
    for (size_t i = 0; i < src.mAttrs.Size(); ++i) {
        EXPECT_STREQ(result.attrs(i).name().c_str(), src.mAttrs[i].mName.CStr());
        EXPECT_STREQ(result.attrs(i).value().c_str(), src.mAttrs[i].mValue.CStr());
    }

    ASSERT_EQ(result.partitions_size(), src.mPartitions.Size());
    for (size_t i = 0; i < src.mPartitions.Size(); ++i) {
        const auto& partition = src.mPartitions[i];
        const auto& proto     = result.partitions(i);

        EXPECT_STREQ(proto.name().c_str(), partition.mName.CStr());
        EXPECT_EQ(proto.total_size(), partition.mTotalSize);
        EXPECT_STREQ(proto.path().c_str(), partition.mPath.CStr());

        ASSERT_EQ(proto.types_size(), partition.mTypes.Size());
        for (size_t j = 0; j < partition.mTypes.Size(); ++j) {
            EXPECT_STREQ(proto.types(j).c_str(), partition.mTypes[j].CStr());
        }
    }

    ASSERT_EQ(result.cpus_size(), src.mCPUs.Size());
    for (size_t i = 0; i < src.mCPUs.Size(); ++i) {
        const auto& cpuInfo = src.mCPUs[i];
        const auto& proto   = result.cpus(i);

        EXPECT_STREQ(proto.model_name().c_str(), cpuInfo.mModelName.CStr());
        EXPECT_EQ(proto.num_cores(), cpuInfo.mNumCores);
        EXPECT_EQ(proto.num_threads(), cpuInfo.mNumThreads);
        EXPECT_STREQ(proto.arch().c_str(), cpuInfo.mArch.CStr());
        EXPECT_STREQ(proto.arch_family().c_str(), cpuInfo.mArchFamily.CStr());
    }
}

} // namespace aos::common::pbconvert
