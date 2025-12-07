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

#include <core/common/tests/mocks/currentnodeinfoprovidermock.hpp>
#include <core/common/tests/utils/log.hpp>

#include <iam/currentnode/currentnodehandler.hpp>

using namespace testing;

namespace aos::iam::currentnode {

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

void SetStateFile(NodeState state)
{
    if (state == NodeStateEnum::eUnprovisioned) {
        std::filesystem::remove(cProvisioningStatePath);

        return;
    }

    std::ofstream file(cProvisioningStatePath);

    if (!file.is_open()) {
        throw std::runtime_error("Failed to create provisioning state file by path: " + cProvisioningStatePath);
    }

    file << state.ToString().CStr();

    file.close();
}

RetWithError<NodeState> GetStateFromFile()
{
    std::ifstream file(cProvisioningStatePath);

    if (!file.is_open()) {
        return NodeState(NodeStateEnum::eUnprovisioned);
    }

    std::string stateStr;

    file >> stateStr;
    file.close();

    NodeState state;

    if (auto err = state.FromString(stateStr.c_str()); !err.IsNone()) {
        return {state, AOS_ERROR_WRAP(err)};
    }

    return state;
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

class CurrentNodeTest : public Test {
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

TEST_F(CurrentNodeTest, InitFailsWithEmptyNodeConfigStruct)
{
    CurrentNodeHandler handler;

    auto err = handler.Init(iam::config::NodeInfoConfig {});
    EXPECT_FALSE(err.IsNone()) << "Init should fail with empty config";
}

TEST_F(CurrentNodeTest, InitFailsIfMemInfoFileNotFound)
{
    iam::config::NodeInfoConfig config = CreateConfig();

    CurrentNodeHandler handler;

    // remove test memory info file
    std::filesystem::remove(cMemInfoPath);

    auto err = handler.Init(config);
    EXPECT_TRUE(err.Is(ErrorEnum::eNotFound)) << "Init should return not found error, err = " << err.Message();
}

TEST_F(CurrentNodeTest, InitFailsIfMemInfoFileIsEmpty)
{
    std::ofstream memInfoFile(cMemInfoPath);
    if (!memInfoFile.is_open()) {
        FAIL() << "Failed to create test memory info file";
    }

    memInfoFile.close();

    CurrentNodeHandler handler;

    auto err = handler.Init(CreateConfig());
    EXPECT_TRUE(err.Is(ErrorEnum::eFailed)) << "Init should return failed error, err = " << err.Message();
}

TEST_F(CurrentNodeTest, InitReturnsDefaultInfoCPUInfoFileNotFound)
{
    CurrentNodeHandler handler;

    // remove test cpu info file
    std::filesystem::remove(cCPUInfoPath);

    auto err = handler.Init(CreateConfig());
    EXPECT_TRUE(err.IsNone());

    NodeInfo nodeInfo;

    err = handler.GetCurrentNodeInfo(nodeInfo);
    ASSERT_TRUE(err.IsNone()) << "GetCurrentNodeInfo should succeed, err = " << err.Message();

    ASSERT_EQ(nodeInfo.mCPUs.Size(), 1) << "Invalid number of CPUs";
    EXPECT_EQ(nodeInfo.mCPUs[0].mNumCores, 1) << "Invalid number of cores";
    EXPECT_EQ(nodeInfo.mCPUs[0].mNumThreads, 1) << "Invalid number of threads";
    EXPECT_STREQ(nodeInfo.mCPUs[0].mArchInfo.mArchitecture.CStr(), GetCPUArch().c_str()) << "Invalid CPU architecture";
}

TEST_F(CurrentNodeTest, InitReturnsDefaultInfoCPUInfoCorrupted)
{
    CurrentNodeHandler handler;

    // remove test cpu info file
    std::ofstream cpuInfoFile(cCPUInfoPath);
    if (!cpuInfoFile.is_open()) {
        FAIL() << "Failed to create test CPU info file";
    }

    cpuInfoFile << cCPUInfoFileCorruptedContent;
    cpuInfoFile.close();

    auto err = handler.Init(CreateConfig());
    EXPECT_TRUE(err.IsNone());

    NodeInfo nodeInfo;

    err = handler.GetCurrentNodeInfo(nodeInfo);
    ASSERT_TRUE(err.IsNone()) << "GetCurrentNodeInfo should succeed, err = " << err.Message();

    ASSERT_EQ(nodeInfo.mCPUs.Size(), 1) << "Invalid number of CPUs";
    EXPECT_EQ(nodeInfo.mCPUs[0].mNumCores, 1) << "Invalid number of cores";
    EXPECT_EQ(nodeInfo.mCPUs[0].mNumThreads, 1) << "Invalid number of threads";
    EXPECT_STREQ(nodeInfo.mCPUs[0].mArchInfo.mArchitecture.CStr(), GetCPUArch().c_str()) << "Invalid CPU architecture";
}

TEST_F(CurrentNodeTest, InitFailsIfConfigAttributesExceedMaxAllowed)
{
    iam::config::NodeInfoConfig config = CreateConfig();

    for (size_t i = 0; i < cMaxNumNodeAttributes + 1; ++i) {
        config.mAttrs[std::to_string(i).append("-name")] = std::to_string(i).append("-value");
    }

    CurrentNodeHandler handler;

    auto err = handler.Init(config);
    EXPECT_TRUE(err.Is(ErrorEnum::eNoMemory)) << "Init should return no memory error, err = " << err.Message();
}

TEST_F(CurrentNodeTest, InitSucceedsOnNonStandardProcFile)
{
    CurrentNodeHandler handler;

    // remove test cpu info file
    std::ofstream cpuInfoFile(cCPUInfoPath);
    if (!cpuInfoFile.is_open()) {
        FAIL() << "Failed to create test CPU info file";
    }

    cpuInfoFile << cEmptyProcFileContent;
    cpuInfoFile.close();

    auto err = handler.Init(CreateConfig());
    ASSERT_TRUE(err.IsNone());

    NodeInfo nodeInfo;

    err = handler.GetCurrentNodeInfo(nodeInfo);
    ASSERT_TRUE(err.IsNone()) << "GetCurrentNodeInfo should succeed, err = " << err.Message();

    ASSERT_EQ(nodeInfo.mCPUs.Size(), 1) << "Invalid number of CPUs";
    EXPECT_EQ(nodeInfo.mCPUs[0].mNumCores, 1) << "Invalid number of cores";
    EXPECT_EQ(nodeInfo.mCPUs[0].mNumThreads, 1) << "Invalid number of threads";

    const auto expectedCPUArch = GetCPUArch();
    EXPECT_STREQ(nodeInfo.mCPUs[0].mArchInfo.mArchitecture.CStr(), expectedCPUArch.c_str())
        << "Invalid CPU architecture";
}

TEST_F(CurrentNodeTest, GetCurrentNodeInfoSucceeds)
{
    const iam::config::NodeInfoConfig config = CreateConfig();

    CurrentNodeHandler handler;
    NodeInfo           nodeInfo;

    auto err = handler.Init(config);
    ASSERT_TRUE(err.IsNone()) << "Init should succeed, err = " << err.Message();

    err = handler.GetCurrentNodeInfo(nodeInfo);
    ASSERT_TRUE(err.IsNone()) << "GetCurrentNodeInfo should succeed, err = " << err.Message();

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

TEST_F(CurrentNodeTest, GetCurrentNodeInfoReadsProvisioningStateFromFile)
{
    const iam::config::NodeInfoConfig config = CreateConfig();

    CurrentNodeHandler handler;
    NodeInfo           nodeInfo;

    auto err = handler.Init(config);
    ASSERT_TRUE(err.IsNone()) << "Init should succeed, err = " << err.Message();

    err = handler.GetCurrentNodeInfo(nodeInfo);
    ASSERT_TRUE(err.IsNone()) << "GetCurrentNodeInfo should succeed, err = " << err.Message();

    EXPECT_EQ(nodeInfo.mState, NodeStateEnum::eUnprovisioned)
        << "Expected unprovisioned state, got: " << nodeInfo.mState;

    SetStateFile(NodeStateEnum::eProvisioned);

    err = handler.Init(config);
    ASSERT_TRUE(err.IsNone()) << "Init should succeed, err = " << err.Message();

    err = handler.GetCurrentNodeInfo(nodeInfo);
    ASSERT_TRUE(err.IsNone()) << "GetCurrentNodeInfo should succeed, err = " << err.Message();

    EXPECT_EQ(nodeInfo.mState, NodeStateEnum::eProvisioned) << "Expected provisioned state, got: " << nodeInfo.mState;
}

TEST_F(CurrentNodeTest, CheckStates)
{
    CurrentNodeHandler handler;

    auto err = handler.Init(CreateConfig());
    ASSERT_TRUE(err.IsNone()) << "Init should succeed, err = " << err.Message();

    struct TestCase {
        std::function<Error()> mAction;
        NodeState              mState;
        bool                   mIsConnected;
    };

    const std::vector<TestCase> testCases = {
        {[&handler]() { return handler.SetState(NodeStateEnum::eProvisioned); }, NodeStateEnum::eProvisioned, false},
        {[&handler]() { return handler.SetState(NodeStateEnum::eProvisioned); }, NodeStateEnum::eProvisioned, false},
        {[&handler]() { return handler.SetConnected(true); }, NodeStateEnum::eProvisioned, true},
        {[&handler]() { return handler.SetConnected(true); }, NodeStateEnum::eProvisioned, true},
        {[&handler]() { return handler.SetState(NodeStateEnum::ePaused); }, NodeStateEnum::ePaused, true},
        {[&handler]() { return handler.SetState(NodeStateEnum::ePaused); }, NodeStateEnum::ePaused, true},
        {[&handler]() { return handler.SetState(NodeStateEnum::eProvisioned); }, NodeStateEnum::eProvisioned, true},
        {[&handler]() { return handler.SetState(NodeStateEnum::eProvisioned); }, NodeStateEnum::eProvisioned, true},
        {[&handler]() { return handler.SetConnected(false); }, NodeStateEnum::eProvisioned, false},
        {[&handler]() { return handler.SetConnected(false); }, NodeStateEnum::eProvisioned, false},
        {[&handler]() { return handler.SetState(NodeStateEnum::ePaused); }, NodeStateEnum::ePaused, false},
        {[&handler]() { return handler.SetState(NodeStateEnum::eProvisioned); }, NodeStateEnum::eProvisioned, false},
        {[&handler]() { return handler.SetConnected(true); }, NodeStateEnum::eProvisioned, true},
        {[&handler]() { return handler.SetState(NodeStateEnum::ePaused); }, NodeStateEnum::ePaused, true},
        {[&handler]() { return handler.SetState(NodeStateEnum::eProvisioned); }, NodeStateEnum::eProvisioned, true},
        {[&handler]() { return handler.SetState(NodeStateEnum::eUnprovisioned); }, NodeStateEnum::eUnprovisioned, true},
        {[&handler]() { return handler.SetState(NodeStateEnum::eUnprovisioned); }, NodeStateEnum::eUnprovisioned, true},
        {[&handler]() { return handler.SetConnected(false); }, NodeStateEnum::eUnprovisioned, false},
    };

    for (size_t i = 0; i < testCases.size(); ++i) {
        LOG_DBG() << "Executing test case: " << (i + 1);

        err = testCases[i].mAction();
        ASSERT_TRUE(err.IsNone()) << "Action should succeed, err = " << err.Message();

        NodeInfo nodeInfo;

        err = handler.GetCurrentNodeInfo(nodeInfo);
        ASSERT_TRUE(err.IsNone()) << "GetCurrentNodeInfo should succeed, err = " << err.Message();

        EXPECT_EQ(nodeInfo.mState, testCases[i].mState)
            << "Invalid node state, expected: " << testCases[i].mState.ToString().CStr()
            << ", got: " << nodeInfo.mState.ToString().CStr();
        EXPECT_EQ(nodeInfo.mIsConnected, testCases[i].mIsConnected)
            << "Invalid connected state, expected: " << testCases[i].mIsConnected << ", got: " << nodeInfo.mIsConnected;

        NodeState fileState;

        Tie(fileState, err) = GetStateFromFile();
        ASSERT_TRUE(err.IsNone()) << "GetStateFromFile should succeed, err = " << err.Message();

        EXPECT_EQ(fileState, nodeInfo.mState)
            << "State in file mismatch, expected: " << nodeInfo.mState.ToString().CStr()
            << ", got: " << fileState.ToString().CStr();
    }
}

TEST_F(CurrentNodeTest, ListenersAreNotNotifiedIfStateNotChanged)
{
    SetStateFile(NodeStateEnum::eUnprovisioned);

    iamclient::CurrentNodeInfoListenerMock listener1, listener2;
    CurrentNodeHandler                     handler;

    auto err = handler.Init(CreateConfig());
    ASSERT_TRUE(err.IsNone()) << "Init should succeed, err=" << err.Message();

    err = handler.SubscribeListener(listener1);
    ASSERT_TRUE(err.IsNone()) << "SubscribeListener should succeed, err=" << err.Message();

    err = handler.SubscribeListener(listener2);
    ASSERT_TRUE(err.IsNone()) << "SubscribeListener should succeed, err=" << err.Message();

    EXPECT_CALL(listener1, OnCurrentNodeInfoChanged(_)).Times(0);
    EXPECT_CALL(listener2, OnCurrentNodeInfoChanged(_)).Times(0);

    err = handler.SetState(NodeStateEnum::eUnprovisioned);
    EXPECT_TRUE(err.IsNone()) << "SetState should succeed, err=" << err.Message();

    err = handler.SetConnected(false);
    EXPECT_TRUE(err.IsNone()) << "SetConnected should succeed, err=" << err.Message();
}

TEST_F(CurrentNodeTest, ObserversAreNotifiedOnStateChange)
{
    SetStateFile(NodeStateEnum::eProvisioned);

    iamclient::CurrentNodeInfoListenerMock listener1, listener2;
    CurrentNodeHandler                     handler;

    auto err = handler.Init(CreateConfig());
    ASSERT_TRUE(err.IsNone()) << "Init should succeed, err=" << err.Message();

    err = handler.SubscribeListener(listener1);
    ASSERT_TRUE(err.IsNone()) << "SubscribeListener should succeed, err=" << err.Message();

    err = handler.SubscribeListener(listener2);
    ASSERT_TRUE(err.IsNone()) << "SubscribeListener should succeed, err=" << err.Message();

    EXPECT_CALL(listener1, OnCurrentNodeInfoChanged(_)).Times(1);
    EXPECT_CALL(listener2, OnCurrentNodeInfoChanged(_)).Times(1);

    err = handler.SetConnected(true);
    EXPECT_TRUE(err.IsNone()) << "SetConnected should succeed, err=" << err.Message();

    // unsubscribe listener1
    err = handler.UnsubscribeListener(listener1);
    ASSERT_TRUE(err.IsNone()) << "UnsubscribeListener should succeed, err=" << err.Message();

    EXPECT_CALL(listener1, OnCurrentNodeInfoChanged(_)).Times(0);
    EXPECT_CALL(listener2, OnCurrentNodeInfoChanged(_)).Times(1);

    err = handler.SetConnected(false);
    EXPECT_TRUE(err.IsNone()) << "SetConnected should succeed, err=" << err.Message();
}

} // namespace aos::iam::currentnode
