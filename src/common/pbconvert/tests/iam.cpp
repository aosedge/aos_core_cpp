/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gmock/gmock.h>

#include <core/common/tests/utils/log.hpp>

#include <common/pbconvert/iam.hpp>

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

    result.mModelName              = modelName.c_str();
    result.mNumCores               = 4;
    result.mNumThreads             = 8;
    result.mArchInfo.mArchitecture = "arch";
    result.mArchInfo.mVariant.SetValue("arch-family");

    return result;
}

NodeInfo CreateNodeInfo()
{
    NodeInfo result;

    result.mNodeID   = "node-id";
    result.mNodeType = "node-type";
    result.mTitle    = "node-title";
    result.mMaxDMIPS = 1024;
    result.mTotalRAM = 2048;
    result.mPhysicalRAM.SetValue(4096);
    result.mProvisioned = true;
    result.mState       = NodeStateEnum::eOnline;

    result.mOSInfo.mOS = "linux";
    result.mOSInfo.mVersion.SetValue("5.10.0");
    result.mOSInfo.mFeatures.PushBack("feature1");
    result.mOSInfo.mFeatures.PushBack("feature2");

    result.mAttrs.PushBack(NodeAttribute {"attr-1", "value-1"});
    result.mAttrs.PushBack(NodeAttribute {"attr-2", "value-2"});

    result.mPartitions.PushBack(CreatePartitionInfo("partition-1"));
    result.mPartitions.PushBack(CreatePartitionInfo("partition-2"));

    result.mCPUs.PushBack(CreateCPUInfo("cpu-1"));
    result.mCPUs.PushBack(CreateCPUInfo("cpu-2"));

    result.mError = Error(ErrorEnum::eFailed, "test error");

    return result;
}

} // namespace

class PBConvertIAMTest : public Test {
public:
    void SetUp() override { tests::utils::InitLog(); }
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(PBConvertIAMTest, ConvertSubjectsToProto)
{
    const StaticString<cIDLen> subjects[] = {"subject-id-1", "subject-id-2"};

    iamanager::v6::Subjects result = ConvertToProto(Array<StaticString<cIDLen>>(subjects, std::size(subjects)));

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

    iamanager::v6::NodeAttribute result = ConvertToProto(src);

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
    EXPECT_STREQ(result.arch_info().architecture().c_str(), src.mArchInfo.mArchitecture.CStr());
    EXPECT_STREQ(result.arch_info().variant().c_str(), src.mArchInfo.mVariant->CStr());
    EXPECT_EQ(result.max_dmips(), 0); // Not set in CreateCPUInfo
}

TEST_F(PBConvertIAMTest, ConvertCPUInfoToProtoWithMaxDMIPS)
{
    CPUInfo src;
    src.mModelName              = "Intel Xeon";
    src.mNumCores               = 8;
    src.mNumThreads             = 16;
    src.mArchInfo.mArchitecture = "x86_64";
    src.mArchInfo.mVariant.SetValue("v3");
    src.mMaxDMIPS.SetValue(2000);

    const auto result = ConvertToProto(src);

    EXPECT_STREQ(result.model_name().c_str(), "Intel Xeon");
    EXPECT_EQ(result.num_cores(), 8);
    EXPECT_EQ(result.num_threads(), 16);
    EXPECT_STREQ(result.arch_info().architecture().c_str(), "x86_64");
    EXPECT_STREQ(result.arch_info().variant().c_str(), "v3");
    EXPECT_EQ(result.max_dmips(), 2000);
}

TEST_F(PBConvertIAMTest, ConvertNodeInfoToProto)
{
    const auto src    = CreateNodeInfo();
    const auto result = ConvertToProto(src);

    EXPECT_STREQ(result.node_id().c_str(), src.mNodeID.CStr());
    EXPECT_STREQ(result.node_type().c_str(), src.mNodeType.CStr());
    EXPECT_STREQ(result.title().c_str(), src.mTitle.CStr());
    EXPECT_EQ(result.max_dmips(), src.mMaxDMIPS);
    EXPECT_EQ(result.total_ram(), src.mTotalRAM);
    EXPECT_EQ(result.physical_ram(), *src.mPhysicalRAM);
    EXPECT_EQ(result.provisioned(), src.mProvisioned);
    EXPECT_STREQ(result.state().c_str(), src.mState.ToString().CStr());

    EXPECT_STREQ(result.os_info().os().c_str(), src.mOSInfo.mOS.CStr());
    EXPECT_STREQ(result.os_info().version().c_str(), src.mOSInfo.mVersion->CStr());
    ASSERT_EQ(result.os_info().features_size(), src.mOSInfo.mFeatures.Size());
    for (size_t i = 0; i < src.mOSInfo.mFeatures.Size(); ++i) {
        EXPECT_STREQ(result.os_info().features(i).c_str(), src.mOSInfo.mFeatures[i].CStr());
    }

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
        EXPECT_STREQ(proto.arch_info().architecture().c_str(), cpuInfo.mArchInfo.mArchitecture.CStr());
        EXPECT_STREQ(proto.arch_info().variant().c_str(), cpuInfo.mArchInfo.mVariant->CStr());
    }

    EXPECT_TRUE(result.has_error());
    EXPECT_EQ(result.error().exit_code(), src.mError.Errno());
    EXPECT_STREQ(result.error().message().c_str(), src.mError.Message());
}

TEST_F(PBConvertIAMTest, ConvertPermissionsResponseToAos)
{
    iamanager::v6::PermissionsResponse src;

    auto* instance = src.mutable_instance();
    instance->set_item_id("test-item");
    instance->set_subject_id("test-subject");
    instance->set_instance(123);

    auto* perms           = src.mutable_permissions()->mutable_permissions();
    (*perms)["function1"] = "rw";
    (*perms)["function2"] = "r";
    (*perms)["function3"] = "w";

    InstanceIdent                                        instanceIdent;
    StaticArray<FunctionPermissions, cFunctionsMaxCount> servicePermissions;

    auto err = ConvertToAos(src, instanceIdent, servicePermissions);

    ASSERT_TRUE(err.IsNone()) << err.Message();

    EXPECT_EQ(instanceIdent.mItemID, String("test-item"));
    EXPECT_EQ(instanceIdent.mSubjectID, String("test-subject"));
    EXPECT_EQ(instanceIdent.mInstance, 123);

    ASSERT_EQ(servicePermissions.Size(), 3);

    bool found1 = false, found2 = false, found3 = false;
    for (const auto& perm : servicePermissions) {
        if (perm.mFunction == String("function1")) {
            EXPECT_EQ(perm.mPermissions, String("rw"));
            found1 = true;
        } else if (perm.mFunction == String("function2")) {
            EXPECT_EQ(perm.mPermissions, String("r"));
            found2 = true;
        } else if (perm.mFunction == String("function3")) {
            EXPECT_EQ(perm.mPermissions, String("w"));
            found3 = true;
        }
    }

    EXPECT_TRUE(found1);
    EXPECT_TRUE(found2);
    EXPECT_TRUE(found3);
}

TEST_F(PBConvertIAMTest, ConvertPermissionsResponseToAosEmpty)
{
    iamanager::v6::PermissionsResponse src;

    auto* instance = src.mutable_instance();
    instance->set_item_id("test-item");
    instance->set_subject_id("test-subject");
    instance->set_instance(1);

    InstanceIdent                                        instanceIdent;
    StaticArray<FunctionPermissions, cFunctionsMaxCount> servicePermissions;

    auto err = ConvertToAos(src, instanceIdent, servicePermissions);

    ASSERT_TRUE(err.IsNone()) << err.Message();

    EXPECT_EQ(instanceIdent.mItemID, String("test-item"));
    EXPECT_EQ(instanceIdent.mSubjectID, String("test-subject"));
    EXPECT_EQ(instanceIdent.mInstance, 1);

    EXPECT_EQ(servicePermissions.Size(), 0);
}

} // namespace aos::common::pbconvert
