/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <array>
#include <filesystem>
#include <fstream>
#include <thread>

#include <Poco/Environment.h>
#include <gmock/gmock.h>

#include <aos/test/log.hpp>

#include "mocks/nodeinfoprovidermock.hpp"
#include "nodeinfoprovider/nodeinfoprovider.hpp"

using namespace testing;

/***********************************************************************************************************************
 * Consts
 **********************************************************************************************************************/

#define TEST_TMP_DIR "test-tmp"

static const std::string     cNodeIDPath             = TEST_TMP_DIR "/node-id";
static const std::string     cProvisioningStatusPath = TEST_TMP_DIR "/provisioning-status";
static const std::string     cCPUInfoPath            = TEST_TMP_DIR "/cpuinfo";
static const std::string     cMemInfoPath            = TEST_TMP_DIR "/meminfo";
static const std::array      cPartitionsInfoConfig {PartitionInfoConfig {"Name1", {"Type1"}, ""}};
static constexpr auto        cNodeIDFileContent           = "node-id";
static constexpr auto        cCPUInfoFileContent          = R"(processor	: 0
cpu family	: 6
model		: 141
model name	: 11th Gen Intel(R) Core(TM) i7-11800H @ 2.30GHz
cpu MHz		: 2304.047
cache size	: 16384 KB
physical id	: 0
siblings	: 1
core id		: 0
cpu cores	: 1

processor	: 1
cpu family	: 6
model		: 141
model name	: 2nd processor model name
cpu MHz		: 2304.047
cache size	: 16384 KB
physical id	: 1
siblings	: 1
core id		: 0
cpu cores	: 1

processor	: 2
cpu family	: 6
model		: 141
model name	: 3nd processor model name
cpu MHz		: 2304.047
cache size	: 16384 KB
physical id	: 2
siblings	: 1
core id		: 0
cpu cores	: 1
)";
static constexpr auto        cCPUInfoFileCorruptedContent = "physical id		: number_is_expected_here";
static constexpr auto        cMemInfoFileContent          = "MemTotal:       16384 kB";
static constexpr auto        cExpectedMemSizeBytes        = 16384 * 1024;
static const aos::NodeStatus cProvisionedStatus           = aos::NodeStatusEnum::eProvisioned;
static const aos::NodeStatus cUnprovisionedStatus         = aos::NodeStatusEnum::eUnprovisioned;

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

static NodeInfoConfig CreateConfig()
{
    NodeInfoConfig config;

    config.mProvisioningStatePath = cProvisioningStatusPath;
    config.mCPUInfoPath           = cCPUInfoPath;
    config.mMemInfoPath           = cMemInfoPath;
    config.mNodeIDPath            = cNodeIDPath;
    config.mNodeName              = "node-name";
    config.mMaxDMIPS              = 1000;

    config.mAttrs      = {{"attr1", "value1"}, {"attr2", "value2"}};
    config.mPartitions = {cPartitionsInfoConfig.cbegin(), cPartitionsInfoConfig.cend()};

    return config;
}

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class NodeInfoProviderTest : public Test {
protected:
    void SetUp() override
    {
        aos::test::InitLog();

        std::filesystem::create_directory(TEST_TMP_DIR);

        std::ofstream cpuInfoFile(cCPUInfoPath);
        if (!cpuInfoFile.is_open()) {
            FAIL() << "Failed to create test CPU info file by path: " << cCPUInfoPath;
        }

        std::ofstream memInfoFile(cMemInfoPath);
        if (!memInfoFile.is_open()) {
            FAIL() << "Failed to create test memory info file by path: " << cMemInfoPath;
        }

        std::ofstream nodeIDFile(cNodeIDPath);
        if (!nodeIDFile.is_open()) {
            FAIL() << "Failed to create test node ID file by path: " << cNodeIDPath;
        }

        cpuInfoFile << cCPUInfoFileContent;
        memInfoFile << cMemInfoFileContent;
        nodeIDFile << cNodeIDFileContent;
    }

    void TearDown() override { std::filesystem::remove_all(TEST_TMP_DIR); }
};

TEST_F(NodeInfoProviderTest, InitFailsWithEmptyNodeConfigStruct)
{
    NodeInfoProvider provider;

    auto err = provider.Init(NodeInfoConfig {});
    EXPECT_FALSE(err.IsNone()) << "Init should fail with empty config";
}

TEST_F(NodeInfoProviderTest, InitFailsIfMemInfoFileNotFound)
{
    NodeInfoConfig config = CreateConfig();

    NodeInfoProvider provider;

    // remove test memory info file
    std::filesystem::remove(cMemInfoPath);

    auto err = provider.Init(config);
    EXPECT_TRUE(err.Is(aos::ErrorEnum::eNotFound)) << "Init should return not found error, err = " << err.Message();
}

TEST_F(NodeInfoProviderTest, InitFailsIfMemInfoFileIsEmpty)
{
    std::ofstream memInfoFile(cMemInfoPath);
    if (!memInfoFile.is_open()) {
        FAIL() << "Failed to create test memory info file";
    }

    memInfoFile.close();

    NodeInfoProvider provider;

    auto err = provider.Init(CreateConfig());
    EXPECT_TRUE(err.Is(aos::ErrorEnum::eFailed)) << "Init should return failed error, err = " << err.Message();
}

TEST_F(NodeInfoProviderTest, InitFailsIfCPUInfoFileNotFound)
{
    NodeInfoProvider provider;

    // remove test cpu info file
    std::filesystem::remove(cCPUInfoPath);

    auto err = provider.Init(CreateConfig());
    EXPECT_TRUE(err.Is(aos::ErrorEnum::eNotFound)) << "Init should return not found error, err = " << err.Message();
}

TEST_F(NodeInfoProviderTest, InitFailsIfCPUInfoCorrupted)
{
    NodeInfoProvider provider;

    // remove test cpu info file
    std::ofstream cpuInfoFile(cCPUInfoPath);
    if (!cpuInfoFile.is_open()) {
        FAIL() << "Failed to create test CPU info file";
    }

    cpuInfoFile << cCPUInfoFileCorruptedContent;
    cpuInfoFile.close();

    auto err = provider.Init(CreateConfig());
    EXPECT_TRUE(err.Is(aos::ErrorEnum::eFailed)) << "Init should return failed error, err = " << err.Message();
}

TEST_F(NodeInfoProviderTest, InitFailsIfConfigAttributesExceedMaxAllowed)
{
    NodeInfoConfig config = CreateConfig();

    for (size_t i = 0; i < aos::cMaxNumNodeAttributes + 1; ++i) {
        config.mAttrs[std::to_string(i).append("-name")] = std::to_string(i).append("-value");
    }

    NodeInfoProvider provider;

    auto err = provider.Init(config);
    EXPECT_TRUE(err.Is(aos::ErrorEnum::eNoMemory)) << "Init should return no memory error, err = " << err.Message();
}

TEST_F(NodeInfoProviderTest, GetNodeInfoSucceeds)
{
    const NodeInfoConfig config = CreateConfig();

    NodeInfoProvider provider;
    aos::NodeInfo    nodeInfo;

    auto err = provider.Init(config);
    ASSERT_TRUE(err.IsNone()) << "Init should succeed, err = " << err.Message();

    err = provider.GetNodeInfo(nodeInfo);
    ASSERT_TRUE(err.IsNone()) << "GetNodeInfo should succeed, err = " << err.Message();

    EXPECT_STREQ(nodeInfo.mNodeID.CStr(), cNodeIDFileContent);
    EXPECT_STREQ(nodeInfo.mNodeType.CStr(), config.mNodeType.c_str());
    EXPECT_STREQ(nodeInfo.mName.CStr(), config.mNodeName.c_str());
    EXPECT_STREQ(nodeInfo.mOSType.CStr(), config.mOSType.c_str());
    EXPECT_EQ(nodeInfo.mTotalRAM, cExpectedMemSizeBytes);

    // check partition info
    ASSERT_EQ(nodeInfo.mPartitions.Size(), cPartitionsInfoConfig.size());
    for (size_t i = 0; i < cPartitionsInfoConfig.size(); ++i) {
        const auto& partitionInfo         = nodeInfo.mPartitions[i];
        const auto& expectedPartitionInfo = cPartitionsInfoConfig[i];

        EXPECT_STREQ(partitionInfo.mName.CStr(), expectedPartitionInfo.mName.c_str());
        EXPECT_STREQ(partitionInfo.mPath.CStr(), expectedPartitionInfo.mPath.c_str());

        ASSERT_EQ(partitionInfo.mTypes.Size(), expectedPartitionInfo.mTypes.size());
        for (size_t j = 0; j < expectedPartitionInfo.mTypes.size(); ++j) {
            EXPECT_STREQ(partitionInfo.mTypes[j].CStr(), expectedPartitionInfo.mTypes[j].c_str());
        }
    }

    for (const auto& nodeAttribute : nodeInfo.mAttrs) {
        const auto it = config.mAttrs.find(nodeAttribute.mName.CStr());

        ASSERT_NE(it, config.mAttrs.end()) << "Attribute not found: " << nodeAttribute.mName.CStr();
        ASSERT_STREQ(nodeAttribute.mValue.CStr(), it->second.c_str())
            << "Attribute value mismatch: " << nodeAttribute.mName.CStr();
    }

    ASSERT_EQ(nodeInfo.mCPUs.Size(), 3) << "Invalid number of CPUs";
}

TEST_F(NodeInfoProviderTest, GetNodeInfoReadsProvisioningStatusFromFile)
{
    const NodeInfoConfig config = CreateConfig();

    NodeInfoProvider provider;
    aos::NodeInfo    nodeInfo;

    auto err = provider.Init(config);
    ASSERT_TRUE(err.IsNone()) << "Init should succeed, err = " << err.Message();

    err = provider.GetNodeInfo(nodeInfo);
    ASSERT_TRUE(err.IsNone()) << "GetNodeInfo should succeed, err = " << err.Message();

    EXPECT_EQ(nodeInfo.mStatus, cUnprovisionedStatus)
        << "Expected unprovisioned status, got: " << nodeInfo.mStatus.ToString().CStr();

    std::ofstream file(cProvisioningStatusPath);
    ASSERT_TRUE(file.is_open()) << "Failed to open provisioning status file, path = " << cProvisioningStatusPath;

    file << cProvisionedStatus.ToString().CStr();
    file.close();

    err = provider.GetNodeInfo(nodeInfo);
    ASSERT_TRUE(err.IsNone()) << "GetNodeInfo should succeed, err = " << err.Message();

    EXPECT_EQ(nodeInfo.mStatus, cProvisionedStatus)
        << "Expected provisioned status, got: " << nodeInfo.mStatus.ToString().CStr();
}

TEST_F(NodeInfoProviderTest, SetNodeStatusFailsIfProvisioningStatusFileNotFound)
{
    NodeInfoProvider provider;

    auto err = provider.SetNodeStatus(aos::NodeStatusEnum::eProvisioned);
    EXPECT_TRUE(err.Is(aos::ErrorEnum::eNotFound))
        << "SetNodeStatus should return not found error, err = " << err.Message();
}

TEST_F(NodeInfoProviderTest, SetNodeStatusSucceeds)
{
    NodeInfoProvider provider;

    NodeInfoConfig config         = CreateConfig();
    config.mProvisioningStatePath = "test-tmp/test-provisioning-status";

    std::remove(config.mProvisioningStatePath.c_str());

    auto err = provider.Init(config);
    ASSERT_TRUE(err.IsNone()) << "Init should succeed, err = " << err.Message();

    err = provider.SetNodeStatus(cProvisionedStatus);
    EXPECT_TRUE(err.IsNone()) << "SetNodeStatus should succeed, err = " << err.Message();

    std::ifstream file(config.mProvisioningStatePath);
    ASSERT_TRUE(file.is_open()) << "Failed to open provisioning status file, path = " << config.mProvisioningStatePath;

    std::string status;
    file >> status;

    EXPECT_STREQ(status.c_str(), cProvisionedStatus.ToString().CStr());
}

TEST_F(NodeInfoProviderTest, ObserversAreNotNotifiedIfStatusNotChanged)
{
    aos::iam::nodeinfoprovider::NodeStatusObserverMock observer1, observer2;

    NodeInfoProvider provider;

    NodeInfoConfig config         = CreateConfig();
    config.mProvisioningStatePath = "test-tmp/test-provisioning-status";

    std::remove(config.mProvisioningStatePath.c_str());

    auto err = provider.Init(config);
    ASSERT_TRUE(err.IsNone()) << "Init should succeed, err=" << err.Message();

    err = provider.SubscribeNodeStatusChanged(observer1);
    ASSERT_TRUE(err.IsNone()) << "SubscribeNodeStatusChanged should succeed, err=" << err.Message();

    err = provider.SubscribeNodeStatusChanged(observer2);
    ASSERT_TRUE(err.IsNone()) << "SubscribeNodeStatusChanged should succeed, err=" << err.Message();

    EXPECT_CALL(observer1, OnNodeStatusChanged(_, _)).Times(0);
    EXPECT_CALL(observer2, OnNodeStatusChanged(_, _)).Times(0);

    err = provider.SetNodeStatus(cUnprovisionedStatus);
    EXPECT_TRUE(err.IsNone()) << "SetNodeStatus should succeed, err=" << err.Message();
}

TEST_F(NodeInfoProviderTest, ObserversAreNotifiedOnStatusChange)
{
    aos::iam::nodeinfoprovider::NodeStatusObserverMock observer1, observer2;

    NodeInfoProvider provider;

    NodeInfoConfig config         = CreateConfig();
    config.mProvisioningStatePath = "test-tmp/test-provisioning-status";

    std::remove(config.mProvisioningStatePath.c_str());

    auto err = provider.Init(config);
    ASSERT_TRUE(err.IsNone()) << "Init should succeed, err=" << err.Message();

    err = provider.SubscribeNodeStatusChanged(observer1);
    ASSERT_TRUE(err.IsNone()) << "SubscribeNodeStatusChanged should succeed, err=" << err.Message();

    err = provider.SubscribeNodeStatusChanged(observer2);
    ASSERT_TRUE(err.IsNone()) << "SubscribeNodeStatusChanged should succeed, err=" << err.Message();

    EXPECT_CALL(observer1, OnNodeStatusChanged(aos::String(cNodeIDFileContent), cProvisionedStatus))
        .WillOnce(Return(aos::ErrorEnum::eNone));
    EXPECT_CALL(observer2, OnNodeStatusChanged(aos::String(cNodeIDFileContent), cProvisionedStatus))
        .WillOnce(Return(aos::ErrorEnum::eNone));

    err = provider.SetNodeStatus(cProvisionedStatus);
    EXPECT_TRUE(err.IsNone()) << "SetNodeStatus should succeed, err=" << err.Message();

    // unsubscribe observer1
    err = provider.UnsubscribeNodeStatusChanged(observer1);
    ASSERT_TRUE(err.IsNone()) << "UnsubscribeNodeStatusChanged should succeed, err=" << err.Message();

    EXPECT_CALL(observer1, OnNodeStatusChanged(_, _)).Times(0);
    EXPECT_CALL(observer2, OnNodeStatusChanged(aos::String(cNodeIDFileContent), cUnprovisionedStatus))
        .WillOnce(Return(aos::ErrorEnum::eNone));

    err = provider.SetNodeStatus(cUnprovisionedStatus);
    EXPECT_TRUE(err.IsNone()) << "SetNodeStatus should succeed, err=" << err.Message();
}
