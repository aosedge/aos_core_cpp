/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <core/common/tests/mocks/currentnodeinfoprovidermock.hpp>
#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>

#include <sm/launcher/runtimes/container/container.hpp>

#include "mocks/filesystemmock.hpp"
#include "mocks/runnermock.hpp"

using namespace testing;

namespace aos::sm::launcher {

namespace {

/***********************************************************************************************************************
 * Consts
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

NodeInfo CreateNodeInfo()
{
    NodeInfo nodeInfo;

    nodeInfo.mNodeID     = "node0";
    nodeInfo.mOSInfo.mOS = "linux";

    nodeInfo.mCPUs.EmplaceBack();
    nodeInfo.mCPUs.Back().mArchInfo.mArchitecture = "amd64";

    return nodeInfo;
}

} // namespace

/***********************************************************************************************************************
 * Structs
 **********************************************************************************************************************/

class TestRuntime : public ContainerRuntime {
public:
    std::shared_ptr<NiceMock<RunnerMock>>     mRunner     = std::make_shared<NiceMock<RunnerMock>>();
    std::shared_ptr<NiceMock<FileSystemMock>> mFileSystem = std::make_shared<NiceMock<FileSystemMock>>();

private:
    std::shared_ptr<RunnerItf>     CreateRunner() override { return mRunner; }
    std::shared_ptr<FileSystemItf> CreateFileSystem() override { return mFileSystem; }
};

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class ContainerRuntimeTest : public Test {
protected:
    static void SetUpTestSuite() { tests::utils::InitLog(); }

    void SetUp() override
    {
        RuntimeConfig config = {"container", "runc", false, "", nullptr};

        EXPECT_CALL(mCurrentNodeInfoProviderMock, GetCurrentNodeInfo(_))
            .WillRepeatedly(DoAll(SetArgReferee<0>(CreateNodeInfo()), Return(ErrorEnum::eNone)));

        auto err = mRuntime.Init(config, mCurrentNodeInfoProviderMock);
        ASSERT_TRUE(err.IsNone()) << "Failed to init runtime: " << tests::utils::ErrorToStr(err);

        err = mRuntime.Start();
        ASSERT_TRUE(err.IsNone()) << "Failed to start runtime: " << tests::utils::ErrorToStr(err);
    }

    void TearDown() override
    {
        auto err = mRuntime.Stop();
        ASSERT_TRUE(err.IsNone()) << "Failed to stop runtime: " << tests::utils::ErrorToStr(err);
    }

    TestRuntime                                      mRuntime;
    NiceMock<iamclient::CurrentNodeInfoProviderMock> mCurrentNodeInfoProviderMock;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(ContainerRuntimeTest, StartInstance)
{
    InstanceInfo instance;

    instance.mItemID    = "item0";
    instance.mSubjectID = "subject0";
    instance.mInstance  = 0;

    auto status = std::make_unique<InstanceStatus>();

    auto err = mRuntime.StartInstance(instance, *status);
    ASSERT_TRUE(err.IsNone()) << "Failed to start instance: " << tests::utils::ErrorToStr(err);

    // Start the same instance again

    err = mRuntime.StartInstance(instance, *status);
    EXPECT_TRUE(err.Is(ErrorEnum::eAlreadyExist)) << "Wrong error: " << tests::utils::ErrorToStr(err);
}

TEST_F(ContainerRuntimeTest, StopInstance)
{
    InstanceInfo instance;

    instance.mItemID    = "item0";
    instance.mSubjectID = "subject0";
    instance.mInstance  = 0;

    auto status = std::make_unique<InstanceStatus>();

    auto err = mRuntime.StartInstance(instance, *status);
    ASSERT_TRUE(err.IsNone()) << "Failed to start instance: " << tests::utils::ErrorToStr(err);

    err = mRuntime.StopInstance(static_cast<const InstanceIdent&>(instance), *status);
    ASSERT_TRUE(err.IsNone()) << "Failed to stop instance: " << tests::utils::ErrorToStr(err);

    // Stop the same instance again

    err = mRuntime.StopInstance(static_cast<const InstanceIdent&>(instance), *status);
    EXPECT_TRUE(err.Is(ErrorEnum::eNotFound)) << "Wrong error: " << tests::utils::ErrorToStr(err);
}

} // namespace aos::sm::launcher
