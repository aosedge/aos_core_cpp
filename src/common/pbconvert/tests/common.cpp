/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gmock/gmock.h>

#include <core/common/tests/utils/log.hpp>

#include <common/pbconvert/common.hpp>

using namespace testing;

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

namespace {

void CompareTimestamps(const aos::Time& lhs, const google::protobuf::Timestamp& rhs)
{
    EXPECT_EQ(lhs.UnixTime().tv_sec, rhs.seconds());
    EXPECT_EQ(lhs.UnixTime().tv_nsec, rhs.nanos());
}

} // namespace

class PBConvertCommon : public Test {
public:
    void SetUp() override { aos::tests::utils::InitLog(); }
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(PBConvertCommon, ConvertAosErrorToProto)
{
    aos::Error params[] = {
        {aos::ErrorEnum::eFailed, "failed error"},
        {aos::ErrorEnum::eRuntime, "runtime error"},
        {aos::ErrorEnum::eNone},
    };

    size_t iteration = 0;

    for (const auto& err : params) {
        LOG_INF() << "Test iteration: " << iteration++;

        auto result = aos::common::pbconvert::ConvertAosErrorToProto(err);

        EXPECT_EQ(result.aos_code(), static_cast<int32_t>(err.Value()));
        EXPECT_EQ(result.exit_code(), err.Errno());
        EXPECT_EQ(aos::String(result.message().c_str()), err.Message()) << err.Message();
    }
}

TEST_F(PBConvertCommon, ConvertAosErrorToGrpcStatus)
{
    aos::Error params[] = {
        {aos::ErrorEnum::eFailed, "failed error"},
        {aos::ErrorEnum::eRuntime, "runtime error"},
        {aos::ErrorEnum::eNone},
    };

    size_t iteration = 0;

    for (const auto& err : params) {
        LOG_INF() << "Test iteration: " << iteration++;

        auto status = aos::common::pbconvert::ConvertAosErrorToGrpcStatus(err);

        if (err.IsNone()) {
            EXPECT_EQ(status.error_code(), grpc::StatusCode::OK);
            EXPECT_TRUE(status.error_message().empty());
        } else {
            EXPECT_EQ(status.error_code(), grpc::StatusCode::INTERNAL);
            EXPECT_STREQ(status.error_message().c_str(), err.Message());
        }
    }
}

TEST_F(PBConvertCommon, ConvertInstanceIdentToProto)
{
    aos::InstanceIdent          param {"item-id", "subject-id", 1, aos::UpdateItemTypeEnum::eComponent};
    ::common::v2::InstanceIdent result = aos::common::pbconvert::ConvertToProto(param);

    EXPECT_EQ(result.item_id(), param.mItemID.CStr());
    EXPECT_EQ(result.subject_id(), param.mSubjectID.CStr());
    EXPECT_EQ(result.instance(), param.mInstance);
    EXPECT_EQ(result.type(), ::common::v2::ItemType::COMPONENT);
}

TEST_F(PBConvertCommon, ConvertInstanceIdentToAos)
{
    ::common::v2::InstanceIdent param;

    param.set_item_id("item-id");
    param.set_subject_id("subject-id");
    param.set_instance(1);

    auto result = aos::common::pbconvert::ConvertToAos(param);

    EXPECT_EQ(result.mItemID, aos::String(param.item_id().c_str()));
    EXPECT_EQ(result.mSubjectID, aos::String(param.subject_id().c_str()));
    EXPECT_EQ(result.mInstance, param.instance());
}

TEST_F(PBConvertCommon, ConvertTimestampToAos)
{
    aos::Optional<aos::Time> expected {aos::Time::Now()};

    google::protobuf::Timestamp param;
    param.set_seconds(expected.GetValue().UnixTime().tv_sec);
    param.set_nanos(expected.GetValue().UnixTime().tv_nsec);

    auto result = aos::common::pbconvert::ConvertToAos(param);
    EXPECT_EQ(result, expected);

    param.Clear();
    expected.Reset();

    result = aos::common::pbconvert::ConvertToAos(param);
    EXPECT_EQ(result, expected);
}

TEST_F(PBConvertCommon, ConvertTimestampToPB)
{
    const auto time = aos::Time::Now();

    auto result = aos::common::pbconvert::TimestampToPB(time);

    CompareTimestamps(time, result);
}

TEST_F(PBConvertCommon, ConvertOSInfoToProto)
{
    aos::OSInfo src;
    src.mOS = "linux";
    src.mVersion.SetValue("5.15.0");
    src.mFeatures.PushBack("feature1");
    src.mFeatures.PushBack("feature2");
    src.mFeatures.PushBack("feature3");

    iamanager::v6::OSInfo dst;
    aos::common::pbconvert::ConvertOSInfoToProto(src, dst);

    EXPECT_STREQ(dst.os().c_str(), "linux");
    EXPECT_STREQ(dst.version().c_str(), "5.15.0");
    ASSERT_EQ(dst.features_size(), 3);
    EXPECT_STREQ(dst.features(0).c_str(), "feature1");
    EXPECT_STREQ(dst.features(1).c_str(), "feature2");
    EXPECT_STREQ(dst.features(2).c_str(), "feature3");
}

TEST_F(PBConvertCommon, ConvertOSInfoToProtoWithoutOptionalFields)
{
    aos::OSInfo src;
    src.mOS = "windows";

    iamanager::v6::OSInfo dst;
    aos::common::pbconvert::ConvertOSInfoToProto(src, dst);

    EXPECT_STREQ(dst.os().c_str(), "windows");
    EXPECT_TRUE(dst.version().empty());
    EXPECT_EQ(dst.features_size(), 0);
}

TEST_F(PBConvertCommon, ConvertNodeInfoToAos)
{
    // Create protobuf NodeInfo
    iamanager::v6::NodeInfo src;
    src.set_node_id("test-node-id");
    src.set_node_type("test-node-type");
    src.set_title("test-title");
    src.set_max_dmips(1000);
    src.set_total_ram(2048);
    src.set_physical_ram(4096);
    src.set_provisioned(true);
    src.set_state("online");

    // Set OSInfo
    auto* osInfo = src.mutable_os_info();
    osInfo->set_os("linux");
    osInfo->set_version("5.10.0");
    osInfo->add_features("feature1");
    osInfo->add_features("feature2");

    // Add CPUs
    auto* cpu1 = src.add_cpus();
    cpu1->set_model_name("Intel Core i7");
    cpu1->set_num_cores(4);
    cpu1->set_num_threads(8);
    cpu1->mutable_arch_info()->set_architecture("x86_64");
    cpu1->mutable_arch_info()->set_variant("v1");
    cpu1->set_max_dmips(500);

    auto* cpu2 = src.add_cpus();
    cpu2->set_model_name("ARM Cortex-A72");
    cpu2->set_num_cores(4);
    cpu2->set_num_threads(4);
    cpu2->mutable_arch_info()->set_architecture("arm64");

    // Add partitions
    auto* partition1 = src.add_partitions();
    partition1->set_name("partition1");
    partition1->set_path("/dev/sda1");
    partition1->set_total_size(1024);
    partition1->add_types("ext4");
    partition1->add_types("data");

    auto* partition2 = src.add_partitions();
    partition2->set_name("partition2");
    partition2->set_path("/dev/sda2");
    partition2->set_total_size(2048);
    partition2->add_types("ext4");

    // Add attributes
    auto* attr1 = src.add_attrs();
    attr1->set_name("attr1");
    attr1->set_value("value1");

    auto* attr2 = src.add_attrs();
    attr2->set_name("attr2");
    attr2->set_value("value2");

    // Add error
    auto* error = src.mutable_error();
    error->set_aos_code(1);
    error->set_exit_code(2);
    error->set_message("test error message");

    // Convert to AOS
    aos::NodeInfo dst;
    auto          err = aos::common::pbconvert::ConvertToAos(src, dst);

    ASSERT_TRUE(err.IsNone()) << err.Message();

    // Verify basic fields
    EXPECT_EQ(dst.mNodeID, aos::String("test-node-id"));
    EXPECT_EQ(dst.mNodeType, aos::String("test-node-type"));
    EXPECT_EQ(dst.mTitle, aos::String("test-title"));
    EXPECT_EQ(dst.mMaxDMIPS, 1000);
    EXPECT_EQ(dst.mTotalRAM, 2048);
    EXPECT_TRUE(dst.mPhysicalRAM.HasValue());
    EXPECT_EQ(*dst.mPhysicalRAM, 4096);
    EXPECT_TRUE(dst.mProvisioned);
    EXPECT_EQ(dst.mState, aos::NodeStateEnum::eOnline);

    // Verify OSInfo
    EXPECT_EQ(dst.mOSInfo.mOS, aos::String("linux"));
    EXPECT_TRUE(dst.mOSInfo.mVersion.HasValue());
    EXPECT_EQ(*dst.mOSInfo.mVersion, aos::String("5.10.0"));
    ASSERT_EQ(dst.mOSInfo.mFeatures.Size(), 2);
    EXPECT_EQ(dst.mOSInfo.mFeatures[0], aos::String("feature1"));
    EXPECT_EQ(dst.mOSInfo.mFeatures[1], aos::String("feature2"));

    // Verify CPUs
    ASSERT_EQ(dst.mCPUs.Size(), 2);
    EXPECT_EQ(dst.mCPUs[0].mModelName, aos::String("Intel Core i7"));
    EXPECT_EQ(dst.mCPUs[0].mNumCores, 4);
    EXPECT_EQ(dst.mCPUs[0].mNumThreads, 8);
    EXPECT_EQ(dst.mCPUs[0].mArchInfo.mArchitecture, aos::String("x86_64"));
    EXPECT_TRUE(dst.mCPUs[0].mArchInfo.mVariant.HasValue());
    EXPECT_EQ(*dst.mCPUs[0].mArchInfo.mVariant, aos::String("v1"));
    EXPECT_TRUE(dst.mCPUs[0].mMaxDMIPS.HasValue());
    EXPECT_EQ(*dst.mCPUs[0].mMaxDMIPS, 500);

    EXPECT_EQ(dst.mCPUs[1].mModelName, aos::String("ARM Cortex-A72"));
    EXPECT_EQ(dst.mCPUs[1].mNumCores, 4);
    EXPECT_EQ(dst.mCPUs[1].mNumThreads, 4);
    EXPECT_EQ(dst.mCPUs[1].mArchInfo.mArchitecture, aos::String("arm64"));
    EXPECT_FALSE(dst.mCPUs[1].mArchInfo.mVariant.HasValue());
    EXPECT_FALSE(dst.mCPUs[1].mMaxDMIPS.HasValue());

    // Verify partitions
    ASSERT_EQ(dst.mPartitions.Size(), 2);
    EXPECT_EQ(dst.mPartitions[0].mName, aos::String("partition1"));
    EXPECT_EQ(dst.mPartitions[0].mPath, aos::String("/dev/sda1"));
    EXPECT_EQ(dst.mPartitions[0].mTotalSize, 1024);
    ASSERT_EQ(dst.mPartitions[0].mTypes.Size(), 2);
    EXPECT_EQ(dst.mPartitions[0].mTypes[0], aos::String("ext4"));
    EXPECT_EQ(dst.mPartitions[0].mTypes[1], aos::String("data"));

    EXPECT_EQ(dst.mPartitions[1].mName, aos::String("partition2"));
    EXPECT_EQ(dst.mPartitions[1].mPath, aos::String("/dev/sda2"));
    EXPECT_EQ(dst.mPartitions[1].mTotalSize, 2048);
    ASSERT_EQ(dst.mPartitions[1].mTypes.Size(), 1);
    EXPECT_EQ(dst.mPartitions[1].mTypes[0], aos::String("ext4"));

    // Verify attributes
    ASSERT_EQ(dst.mAttrs.Size(), 2);
    EXPECT_EQ(dst.mAttrs[0].mName, aos::String("attr1"));
    EXPECT_EQ(dst.mAttrs[0].mValue, aos::String("value1"));
    EXPECT_EQ(dst.mAttrs[1].mName, aos::String("attr2"));
    EXPECT_EQ(dst.mAttrs[1].mValue, aos::String("value2"));

    // Verify error
    EXPECT_FALSE(dst.mError.IsNone());
    EXPECT_EQ(dst.mError.Errno(), 2);
    EXPECT_EQ(aos::String(dst.mError.Message()), aos::String("test error message"));
}

TEST_F(PBConvertCommon, ConvertNodeInfoToAosWithoutOptionalFields)
{
    // Create protobuf NodeInfo with minimal fields
    iamanager::v6::NodeInfo src;
    src.set_node_id("minimal-node");
    src.set_node_type("minimal-type");
    src.set_title("minimal-title");
    src.set_max_dmips(100);
    src.set_total_ram(512);
    src.set_provisioned(false);
    src.set_state("offline");

    // Set minimal OSInfo
    auto* osInfo = src.mutable_os_info();
    osInfo->set_os("linux");

    // Convert to AOS
    aos::NodeInfo dst;
    auto          err = aos::common::pbconvert::ConvertToAos(src, dst);

    ASSERT_TRUE(err.IsNone()) << err.Message();

    // Verify basic fields
    EXPECT_EQ(dst.mNodeID, aos::String("minimal-node"));
    EXPECT_EQ(dst.mNodeType, aos::String("minimal-type"));
    EXPECT_EQ(dst.mTitle, aos::String("minimal-title"));
    EXPECT_EQ(dst.mMaxDMIPS, 100);
    EXPECT_EQ(dst.mTotalRAM, 512);
    EXPECT_FALSE(dst.mPhysicalRAM.HasValue());
    EXPECT_FALSE(dst.mProvisioned);
    EXPECT_EQ(dst.mState, aos::NodeStateEnum::eOffline);

    // Verify OSInfo
    EXPECT_EQ(dst.mOSInfo.mOS, aos::String("linux"));
    EXPECT_FALSE(dst.mOSInfo.mVersion.HasValue());
    EXPECT_EQ(dst.mOSInfo.mFeatures.Size(), 0);

    // Verify empty arrays
    EXPECT_EQ(dst.mCPUs.Size(), 0);
    EXPECT_EQ(dst.mPartitions.Size(), 0);
    EXPECT_EQ(dst.mAttrs.Size(), 0);

    // Verify no error
    EXPECT_TRUE(dst.mError.IsNone());
}
