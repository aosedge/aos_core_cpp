/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <core/common/tests/mocks/currentnodeinfoprovidermock.hpp>
#include <core/common/tests/mocks/ocispecmock.hpp>
#include <core/common/tests/mocks/permhandlermock.hpp>
#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>
#include <core/sm/tests/mocks/instancestatusreceivermock.hpp>
#include <core/sm/tests/mocks/iteminfoprovidermock.hpp>
#include <core/sm/tests/mocks/networkmanagermock.hpp>

#include <common/utils/utils.hpp>

#include <sm/launcher/runtimes.hpp>
#include <sm/launcher/runtimes/boot/boot.hpp>
#include <sm/launcher/runtimes/container/container.hpp>
#include <sm/launcher/runtimes/rootfs/rootfs.hpp>
#include <sm/tests/mocks/systemdconnmock.hpp>

using namespace testing;

namespace aos::sm::launcher {

namespace {

/***********************************************************************************************************************
 * Consts
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

void CreateNodeInfo(NodeInfo& nodeInfo)
{
    nodeInfo.mNodeID     = "1234";
    nodeInfo.mOSInfo.mOS = "linux";

    CPUInfo cpuInfo;

    cpuInfo.mArchInfo.mArchitecture = "amd64";
    nodeInfo.mCPUs.EmplaceBack(cpuInfo);
}

} // namespace

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class RuntimesTest : public Test {
protected:
    static void SetUpTestSuite() { tests::utils::InitLog(); }

    void SetUp() override { }

    void TearDown() override { }

    NiceMock<iamclient::CurrentNodeInfoProviderMock> mCurrentNodeInfoProviderMock;
    NiceMock<imagemanager::ItemInfoProviderMock>     mItemInfoProviderMock;
    NiceMock<networkmanager::NetworkManagerMock>     mNetworkManagerMock;
    NiceMock<iamclient::PermHandlerMock>             mPermHandlerMock;
    NiceMock<oci::OCISpecMock>                       mOCISpecMock;
    NiceMock<InstanceStatusReceiverMock>             mInstanceStatusReceiverMock;
    NiceMock<sm::utils::SystemdConnMock>             mSystemdConnMock;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(RuntimesTest, InitNoRuntimes)
{
    Runtimes runtimes;

    auto err = runtimes.Init(Config {}, mCurrentNodeInfoProviderMock, mItemInfoProviderMock, mNetworkManagerMock,
        mPermHandlerMock, mOCISpecMock, mInstanceStatusReceiverMock, mSystemdConnMock);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    auto runtimesArray = std::make_unique<StaticArray<RuntimeItf*, cMaxNumNodeRuntimes>>();

    err = runtimes.GetRuntimes(*runtimesArray);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_TRUE(runtimesArray->IsEmpty());
}

TEST_F(RuntimesTest, InitRuntimes)
{
    Runtimes runtimes;
    Config   config;

    config.mRuntimes.emplace_back(
        RuntimeConfig {cRuntimeContainer, "crun", false, "", Poco::makeShared<Poco::JSON::Object>()});
    config.mRuntimes.emplace_back(
        RuntimeConfig {cRuntimeBoot, "aos-vm-boot", true, "", Poco::makeShared<Poco::JSON::Object>()});
    config.mRuntimes.emplace_back(
        RuntimeConfig {cRuntimeRootfs, "aos-vm-rootfs", true, "", Poco::makeShared<Poco::JSON::Object>()});
    auto nodeInfo = std::make_unique<NodeInfo>();

    CreateNodeInfo(*nodeInfo);

    EXPECT_CALL(mCurrentNodeInfoProviderMock, GetCurrentNodeInfo(_))
        .WillRepeatedly(DoAll(SetArgReferee<0>(*nodeInfo), Return(ErrorEnum::eNone)));

    auto err = runtimes.Init(config, mCurrentNodeInfoProviderMock, mItemInfoProviderMock, mNetworkManagerMock,
        mPermHandlerMock, mOCISpecMock, mInstanceStatusReceiverMock, mSystemdConnMock);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    auto runtimesArray = std::make_unique<StaticArray<RuntimeItf*, cMaxNumNodeRuntimes>>();

    err = runtimes.GetRuntimes(*runtimesArray);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    ASSERT_EQ(runtimesArray->Size(), config.mRuntimes.size());

    for (auto runtime : *runtimesArray) {
        auto runtimeInfo = std::make_unique<RuntimeInfo>();

        err = runtime->GetRuntimeInfo(*runtimeInfo);
        ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

        auto it = std::find_if(config.mRuntimes.begin(), config.mRuntimes.end(),
            [&runtimeInfo](const auto& config) { return runtimeInfo->mRuntimeType == config.mType.c_str(); });
        ASSERT_TRUE(it != config.mRuntimes.end());

        EXPECT_EQ(runtimeInfo->mRuntimeID, common::utils::NameUUID(it->mType + "-" + nodeInfo->mNodeID.CStr()).c_str());
    }
}

} // namespace aos::sm::launcher
