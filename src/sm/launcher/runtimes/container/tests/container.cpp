/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <core/common/tests/mocks/currentnodeinfoprovidermock.hpp>
#include <core/common/tests/mocks/ocispecmock.hpp>
#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>
#include <core/sm/tests/mocks/iteminfoprovidermock.hpp>

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

std::string CreateInstanceID(const InstanceIdent& instanceIdent)
{
    auto idStr = std::string(instanceIdent.mItemID.CStr()) + ":" + std::string(instanceIdent.mSubjectID.CStr()) + ":"
        + std::to_string(instanceIdent.mInstance);

    return common::utils::NameUUID(idStr);
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

        EXPECT_CALL(*mRuntime.mFileSystem, CreateHostFSWhiteouts(_, _)).WillOnce(Return(ErrorEnum::eNone));

        auto err = mRuntime.Init(config, mCurrentNodeInfoProviderMock, mItemInfoProviderMock, mOCISpecMock);
        ASSERT_TRUE(err.IsNone()) << "Failed to init runtime: " << tests::utils::ErrorToStr(err);

        EXPECT_CALL(*mRuntime.mFileSystem, ListDir(_)).WillOnce(Invoke([](const std::string&) {
            std::vector<std::string> instances;

            return RetWithError<std::vector<std::string>> {instances, ErrorEnum::eNone};
        }));

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
    NiceMock<imagemanager::ItemInfoProviderMock>     mItemInfoProviderMock;
    NiceMock<oci::OCISpecMock>                       mOCISpecMock;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(ContainerRuntimeTest, StopActiveInstances)
{
    EXPECT_CALL(*mRuntime.mFileSystem, ListDir(_)).WillOnce(Invoke([](const std::string&) {
        std::vector<std::string> instances = {"instance1", "instance2", "instance3"};

        return RetWithError<std::vector<std::string>> {instances, ErrorEnum::eNone};
    }));

    EXPECT_CALL(*mRuntime.mRunner, StopInstance("instance1")).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mRuntime.mRunner, StopInstance("instance2")).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mRuntime.mRunner, StopInstance("instance3")).WillOnce(Return(ErrorEnum::eNone));

    auto err = mRuntime.Start();
    ASSERT_TRUE(err.IsNone()) << "Failed to start runtime: " << tests::utils::ErrorToStr(err);
}

TEST_F(ContainerRuntimeTest, StartInstance)
{
    InstanceInfo instance;

    instance.mItemID    = "item0";
    instance.mSubjectID = "subject0";
    instance.mInstance  = 0;

    auto instanceID = CreateInstanceID(static_cast<const InstanceIdent&>(instance));
    auto status     = std::make_unique<InstanceStatus>();

    EXPECT_CALL(*mRuntime.mRunner, StartInstance(instanceID, _))
        .WillOnce(Return(RunStatus {"", InstanceStateEnum::eActive, ErrorEnum::eNone}));

    auto err = mRuntime.StartInstance(instance, *status);
    ASSERT_TRUE(err.IsNone()) << "Failed to start instance: " << tests::utils::ErrorToStr(err);

    EXPECT_EQ(status->mState, InstanceStateEnum::eActive);

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

    auto instanceID = CreateInstanceID(static_cast<const InstanceIdent&>(instance));
    auto status     = std::make_unique<InstanceStatus>();

    auto err = mRuntime.StartInstance(instance, *status);
    ASSERT_TRUE(err.IsNone()) << "Failed to start instance: " << tests::utils::ErrorToStr(err);

    EXPECT_CALL(*mRuntime.mRunner, StopInstance(instanceID)).WillOnce(Return(ErrorEnum::eNone));

    err = mRuntime.StopInstance(static_cast<const InstanceIdent&>(instance), *status);
    ASSERT_TRUE(err.IsNone()) << "Failed to stop instance: " << tests::utils::ErrorToStr(err);

    EXPECT_EQ(status->mState, InstanceStateEnum::eInactive);

    // Stop the same instance again

    err = mRuntime.StopInstance(static_cast<const InstanceIdent&>(instance), *status);
    EXPECT_TRUE(err.Is(ErrorEnum::eNotFound)) << "Wrong error: " << tests::utils::ErrorToStr(err);
}

TEST_F(ContainerRuntimeTest, RuntimeConfig)
{
    InstanceInfo instance;

    instance.mItemID    = "item0";
    instance.mSubjectID = "subject0";
    instance.mInstance  = 0;

    auto instanceID    = CreateInstanceID(static_cast<const InstanceIdent&>(instance));
    auto status        = std::make_unique<InstanceStatus>();
    auto runtimeConfig = std::make_unique<oci::RuntimeConfig>();

    EXPECT_CALL(mOCISpecMock, SaveRuntimeConfig(_, _))
        .WillOnce(Invoke([&runtimeConfig](const String&, const oci::RuntimeConfig& config) {
            *runtimeConfig = config;

            return ErrorEnum::eNone;
        }));

    auto err = mRuntime.StartInstance(instance, *status);
    ASSERT_TRUE(err.IsNone()) << "Failed to start instance: " << tests::utils::ErrorToStr(err);

    // Check process

    ASSERT_TRUE(runtimeConfig->mProcess.HasValue());
    EXPECT_FALSE(runtimeConfig->mProcess->mTerminal);
    EXPECT_EQ(runtimeConfig->mProcess->mUser.mUID, instance.mUID);
    EXPECT_EQ(runtimeConfig->mProcess->mUser.mGID, instance.mGID);

    // Check cgroups path

    ASSERT_TRUE(runtimeConfig->mLinux.HasValue());
    EXPECT_EQ(
        runtimeConfig->mLinux->mCgroupsPath, ("/system.slice/system-aos\\x2dservice.slice/" + instanceID).c_str());
}

} // namespace aos::sm::launcher
