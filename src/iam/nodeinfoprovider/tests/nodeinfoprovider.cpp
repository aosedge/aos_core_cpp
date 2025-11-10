/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <array>
#include <filesystem>
#include <fstream>
#include <sys/utsname.h>
#include <thread>

#include <Poco/Environment.h>
#include <gmock/gmock.h>

#include <core/common/tests/utils/log.hpp>
#include <core/iam/tests/mocks/nodeinfoprovidermock.hpp>

#include <iam/nodeinfoprovider/nodeinfoprovider.hpp>

using namespace testing;

namespace aos::iam::nodeinfoprovider {

namespace {

/***********************************************************************************************************************
 * Consts
 **********************************************************************************************************************/

#define TEST_TMP_DIR "test-tmp"

const std::string cNodeIDPath            = TEST_TMP_DIR "/node-id";
const std::string cProvisioningStatePath = TEST_TMP_DIR "/provisioning-state";
const std::string cCPUInfoPath           = TEST_TMP_DIR "/cpuinfo";
const std::string cMemInfoPath           = TEST_TMP_DIR "/meminfo";
const std::array  cPartitionsInfoConfig {iam::config::PartitionInfoConfig {"Name1", {"Type1"}, ""}};
constexpr auto    cNodeIDFileContent           = "node-id";
constexpr auto    cCPUInfoFileContent          = R"(processor	: 0
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
constexpr auto    cCPUInfoFileCorruptedContent = "physical id		: number_is_expected_here";
constexpr auto    cEmptyProcFileContent        = R"()";
constexpr auto    cMemInfoFileContent          = "MemTotal:       16384 kB";
constexpr auto    cExpectedMemSizeBytes        = 16384 * 1024;
const NodeState   cProvisionedState            = NodeStateEnum::eOnline;
const NodeState   cUnprovisionedState          = NodeStateEnum::eOffline;

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

iam::config::NodeInfoConfig CreateConfig()
{
    iam::config::NodeInfoConfig config;

    config.mProvisioningStatePath = cProvisioningStatePath;
    config.mCPUInfoPath           = cCPUInfoPath;
    config.mMemInfoPath           = cMemInfoPath;
    config.mNodeIDPath            = cNodeIDPath;
    config.mNodeName              = "node-name";
    config.mMaxDMIPS              = 1000;
    config.mOSType                = "testOS";

    config.mAttrs      = {{"attr1", "value1"}, {"attr2", "value2"}};
    config.mPartitions = {cPartitionsInfoConfig.cbegin(), cPartitionsInfoConfig.cend()};

    return config;
}

std::string GetCPUArch()
{
    struct utsname buffer;

    if (auto ret = uname(&buffer); ret != 0) {
        return "unknown";
    }

    return buffer.machine;
}

} // namespace

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class NodeInfoProviderTest : public Test {
protected:
    void SetUp() override
    {
        tests::utils::InitLog();

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

    auto err = provider.Init(iam::config::NodeInfoConfig {});
    EXPECT_FALSE(err.IsNone()) << "Init should fail with empty config";
}

TEST_F(NodeInfoProviderTest, InitFailsIfMemInfoFileNotFound)
{
    iam::config::NodeInfoConfig config = CreateConfig();

    NodeInfoProvider provider;

    // remove test memory info file
    std::filesystem::remove(cMemInfoPath);

    auto err = provider.Init(config);
    EXPECT_TRUE(err.Is(ErrorEnum::eNotFound)) << "Init should return not found error, err = " << err.Message();
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
    EXPECT_TRUE(err.Is(ErrorEnum::eFailed)) << "Init should return failed error, err = " << err.Message();
}

TEST_F(NodeInfoProviderTest, InitReturnsDefaultInfoCPUInfoFileNotFound)
{
    NodeInfoProvider provider;

    // remove test cpu info file
    std::filesystem::remove(cCPUInfoPath);

    auto err = provider.Init(CreateConfig());
    EXPECT_TRUE(err.IsNone());

    NodeInfo nodeInfo;

    err = provider.GetNodeInfo(nodeInfo);
    ASSERT_TRUE(err.IsNone()) << "GetNodeInfo should succeed, err = " << err.Message();

    ASSERT_EQ(nodeInfo.mCPUs.Size(), 1) << "Invalid number of CPUs";
    EXPECT_EQ(nodeInfo.mCPUs[0].mNumCores, 1) << "Invalid number of cores";
    EXPECT_EQ(nodeInfo.mCPUs[0].mNumThreads, 1) << "Invalid number of threads";
    EXPECT_STREQ(nodeInfo.mCPUs[0].mArchInfo.mArchitecture.CStr(), GetCPUArch().c_str()) << "Invalid CPU architecture";
}

TEST_F(NodeInfoProviderTest, InitReturnsDefaultInfoCPUInfoCorrupted)
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
    EXPECT_TRUE(err.IsNone());

    NodeInfo nodeInfo;

    err = provider.GetNodeInfo(nodeInfo);
    ASSERT_TRUE(err.IsNone()) << "GetNodeInfo should succeed, err = " << err.Message();

    ASSERT_EQ(nodeInfo.mCPUs.Size(), 1) << "Invalid number of CPUs";
    EXPECT_EQ(nodeInfo.mCPUs[0].mNumCores, 1) << "Invalid number of cores";
    EXPECT_EQ(nodeInfo.mCPUs[0].mNumThreads, 1) << "Invalid number of threads";
    EXPECT_STREQ(nodeInfo.mCPUs[0].mArchInfo.mArchitecture.CStr(), GetCPUArch().c_str()) << "Invalid CPU architecture";
}

TEST_F(NodeInfoProviderTest, InitFailsIfConfigAttributesExceedMaxAllowed)
{
    iam::config::NodeInfoConfig config = CreateConfig();

    for (size_t i = 0; i < cMaxNumNodeAttributes + 1; ++i) {
        config.mAttrs[std::to_string(i).append("-name")] = std::to_string(i).append("-value");
    }

    NodeInfoProvider provider;

    auto err = provider.Init(config);
    EXPECT_TRUE(err.Is(ErrorEnum::eNoMemory)) << "Init should return no memory error, err = " << err.Message();
}

TEST_F(NodeInfoProviderTest, InitSucceedsOnNonStandardProcFile)
{
    NodeInfoProvider provider;

    // remove test cpu info file
    std::ofstream cpuInfoFile(cCPUInfoPath);
    if (!cpuInfoFile.is_open()) {
        FAIL() << "Failed to create test CPU info file";
    }

    cpuInfoFile << cEmptyProcFileContent;
    cpuInfoFile.close();

    auto err = provider.Init(CreateConfig());
    ASSERT_TRUE(err.IsNone());

    NodeInfo nodeInfo;

    err = provider.GetNodeInfo(nodeInfo);
    ASSERT_TRUE(err.IsNone()) << "GetNodeInfo should succeed, err = " << err.Message();

    ASSERT_EQ(nodeInfo.mCPUs.Size(), 1) << "Invalid number of CPUs";
    EXPECT_EQ(nodeInfo.mCPUs[0].mNumCores, 1) << "Invalid number of cores";
    EXPECT_EQ(nodeInfo.mCPUs[0].mNumThreads, 1) << "Invalid number of threads";

    const auto expectedCPUArch = GetCPUArch();
    EXPECT_STREQ(nodeInfo.mCPUs[0].mArchInfo.mArchitecture.CStr(), expectedCPUArch.c_str())
        << "Invalid CPU architecture";
}

TEST_F(NodeInfoProviderTest, GetNodeInfoSucceeds)
{
    const iam::config::NodeInfoConfig config = CreateConfig();

    NodeInfoProvider provider;
    NodeInfo         nodeInfo;

    auto err = provider.Init(config);
    ASSERT_TRUE(err.IsNone()) << "Init should succeed, err = " << err.Message();

    err = provider.GetNodeInfo(nodeInfo);
    ASSERT_TRUE(err.IsNone()) << "GetNodeInfo should succeed, err = " << err.Message();

    EXPECT_STREQ(nodeInfo.mNodeID.CStr(), cNodeIDFileContent);
    EXPECT_STREQ(nodeInfo.mNodeType.CStr(), config.mNodeType.c_str());
    EXPECT_STREQ(nodeInfo.mTitle.CStr(), config.mNodeName.c_str());
    EXPECT_STREQ(nodeInfo.mOSInfo.mOS.CStr(), config.mOSType.c_str());
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

TEST_F(NodeInfoProviderTest, GetNodeInfoReadsProvisioningStateFromFile)
{
    const iam::config::NodeInfoConfig config = CreateConfig();

    NodeInfoProvider provider;
    NodeInfo         nodeInfo;

    auto err = provider.Init(config);
    ASSERT_TRUE(err.IsNone()) << "Init should succeed, err = " << err.Message();

    err = provider.GetNodeInfo(nodeInfo);
    ASSERT_TRUE(err.IsNone()) << "GetNodeInfo should succeed, err = " << err.Message();

    EXPECT_EQ(nodeInfo.mState, cUnprovisionedState)
        << "Expected unprovisioned state, got: " << nodeInfo.mState.ToString().CStr();

    std::ofstream file(cProvisioningStatePath);
    ASSERT_TRUE(file.is_open()) << "Failed to open provisioning state file, path = " << cProvisioningStatePath;

    file << cProvisionedState.ToString().CStr();
    file.close();

    err = provider.GetNodeInfo(nodeInfo);
    ASSERT_TRUE(err.IsNone()) << "GetNodeInfo should succeed, err = " << err.Message();

    EXPECT_EQ(nodeInfo.mState, cProvisionedState)
        << "Expected provisioned state, got: " << nodeInfo.mState.ToString().CStr();
}

TEST_F(NodeInfoProviderTest, SetNodeStateFailsIfProvisioningStateFileNotFound)
{
    NodeInfoProvider provider;

    auto err = provider.SetNodeState(cProvisionedState, true);
    EXPECT_TRUE(err.Is(ErrorEnum::eNotFound)) << "SetNodeState should return not found error, err = " << err.Message();
}

TEST_F(NodeInfoProviderTest, SetNodeStateSucceeds)
{
    NodeInfoProvider provider;

    iam::config::NodeInfoConfig config = CreateConfig();
    config.mProvisioningStatePath      = "test-tmp/test-provisioning-state";

    std::remove(config.mProvisioningStatePath.c_str());

    auto err = provider.Init(config);
    ASSERT_TRUE(err.IsNone()) << "Init should succeed, err = " << err.Message();

    err = provider.SetNodeState(cProvisionedState, true);
    EXPECT_TRUE(err.IsNone()) << "SetNodeState should succeed, err = " << err.Message();

    std::ifstream file(config.mProvisioningStatePath);
    ASSERT_TRUE(file.is_open()) << "Failed to open provisioning state file, path = " << config.mProvisioningStatePath;

    std::string state;

    file >> state;

    EXPECT_STREQ(state.c_str(), cProvisionedState.ToString().CStr());
}

TEST_F(NodeInfoProviderTest, ObserversAreNotNotifiedIfStateNotChanged)
{
    iam::nodeinfoprovider::NodeStateObserverMock observer1, observer2;

    NodeInfoProvider provider;

    iam::config::NodeInfoConfig config = CreateConfig();
    config.mProvisioningStatePath      = "test-tmp/test-provisioning-state";

    std::remove(config.mProvisioningStatePath.c_str());

    auto err = provider.Init(config);
    ASSERT_TRUE(err.IsNone()) << "Init should succeed, err=" << err.Message();

    err = provider.SubscribeNodeStateChanged(observer1);
    ASSERT_TRUE(err.IsNone()) << "SubscribeNodeStateChanged should succeed, err=" << err.Message();

    err = provider.SubscribeNodeStateChanged(observer2);
    ASSERT_TRUE(err.IsNone()) << "SubscribeNodeStateChanged should succeed, err=" << err.Message();

    EXPECT_CALL(observer1, OnNodeStateChanged(_, _)).Times(0);
    EXPECT_CALL(observer2, OnNodeStateChanged(_, _)).Times(0);

    err = provider.SetNodeState(cUnprovisionedState, false);
    EXPECT_TRUE(err.IsNone()) << "SetNodeState should succeed, err=" << err.Message();
}

TEST_F(NodeInfoProviderTest, ObserversAreNotifiedOnStateChange)
{
    iam::nodeinfoprovider::NodeStateObserverMock observer1, observer2;

    NodeInfoProvider provider;

    iam::config::NodeInfoConfig config = CreateConfig();
    config.mProvisioningStatePath      = "test-tmp/test-provisioning-state";

    std::remove(config.mProvisioningStatePath.c_str());

    auto err = provider.Init(config);
    ASSERT_TRUE(err.IsNone()) << "Init should succeed, err=" << err.Message();

    err = provider.SubscribeNodeStateChanged(observer1);
    ASSERT_TRUE(err.IsNone()) << "SubscribeNodeStateChanged should succeed, err=" << err.Message();

    err = provider.SubscribeNodeStateChanged(observer2);
    ASSERT_TRUE(err.IsNone()) << "SubscribeNodeStateChanged should succeed, err=" << err.Message();

    EXPECT_CALL(observer1, OnNodeStateChanged(String(cNodeIDFileContent), cProvisionedState))
        .WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(observer2, OnNodeStateChanged(String(cNodeIDFileContent), cProvisionedState))
        .WillOnce(Return(ErrorEnum::eNone));

    err = provider.SetNodeState(cProvisionedState, true);
    EXPECT_TRUE(err.IsNone()) << "SetNodeState should succeed, err=" << err.Message();

    // unsubscribe observer1
    err = provider.UnsubscribeNodeStateChanged(observer1);
    ASSERT_TRUE(err.IsNone()) << "UnsubscribeNodeStateChanged should succeed, err=" << err.Message();

    EXPECT_CALL(observer1, OnNodeStateChanged(_, _)).Times(0);
    EXPECT_CALL(observer2, OnNodeStateChanged(String(cNodeIDFileContent), cUnprovisionedState))
        .WillOnce(Return(ErrorEnum::eNone));

    err = provider.SetNodeState(cUnprovisionedState, false);
    EXPECT_TRUE(err.IsNone()) << "SetNodeState should succeed, err=" << err.Message();
}

} // namespace aos::iam::nodeinfoprovider
