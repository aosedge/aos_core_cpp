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
#include <core/sm/tests/mocks/iteminfoprovidermock.hpp>
#include <core/sm/tests/mocks/networkmanagermock.hpp>

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
    nodeInfo.mMaxDMIPS   = 10000;
    nodeInfo.mCPUs.EmplaceBack(CPUInfo {"amd64", 4, 2500, {}, {}});

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

Error CheckMount(const oci::RuntimeConfig& runtimeConfig, const Mount& mount)
{
    if (auto it = std::find(runtimeConfig.mMounts.begin(), runtimeConfig.mMounts.end(), mount);
        it == runtimeConfig.mMounts.end()) {
        return ErrorEnum::eNotFound;
    }

    return ErrorEnum::eNone;
}

Error CheckNameSpace(const oci::RuntimeConfig& runtimeConfig, const oci::LinuxNamespace& ns)
{
    if (auto it = std::find(runtimeConfig.mLinux->mNamespaces.begin(), runtimeConfig.mLinux->mNamespaces.end(), ns);
        it == runtimeConfig.mLinux->mNamespaces.end()) {
        return ErrorEnum::eNotFound;
    }

    return ErrorEnum::eNone;
}

Error CheckEnvVar(const oci::RuntimeConfig& runtimeConfig, const std::string& envVar)
{
    if (auto it = std::find(runtimeConfig.mProcess->mEnv.begin(), runtimeConfig.mProcess->mEnv.end(), envVar.c_str());
        it == runtimeConfig.mProcess->mEnv.end()) {
        return ErrorEnum::eNotFound;
    }

    return ErrorEnum::eNone;
}

Error CheckRLimits(const oci::RuntimeConfig& runtimeConfig, const oci::POSIXRlimit& rLimit)
{
    auto it = std::find(runtimeConfig.mProcess->mRlimits.begin(), runtimeConfig.mProcess->mRlimits.end(), rLimit);
    if (it == runtimeConfig.mProcess->mRlimits.end()) {
        return ErrorEnum::eNotFound;
    }

    return ErrorEnum::eNone;
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

        mNodeInfo = CreateNodeInfo();

        EXPECT_CALL(mCurrentNodeInfoProviderMock, GetCurrentNodeInfo(_))
            .WillRepeatedly(DoAll(SetArgReferee<0>(mNodeInfo), Return(ErrorEnum::eNone)));

        EXPECT_CALL(*mRuntime.mFileSystem, CreateHostFSWhiteouts(_, _)).WillOnce(Return(ErrorEnum::eNone));

        auto err = mRuntime.Init(config, mCurrentNodeInfoProviderMock, mItemInfoProviderMock, mNetworkManagerMock,
            mPermHandlerMock, mOCISpecMock);
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
    NodeInfo                                         mNodeInfo;
    NiceMock<iamclient::CurrentNodeInfoProviderMock> mCurrentNodeInfoProviderMock;
    NiceMock<imagemanager::ItemInfoProviderMock>     mItemInfoProviderMock;
    NiceMock<networkmanager::NetworkManagerMock>     mNetworkManagerMock;
    NiceMock<iamclient::PermHandlerMock>             mPermHandlerMock;
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

    // Check root

    ASSERT_TRUE(runtimeConfig->mRoot.HasValue());
    EXPECT_EQ(runtimeConfig->mRoot->mPath, ("/run/aos/runtime/" + instanceID + "/rootfs").c_str());
    EXPECT_FALSE(runtimeConfig->mRoot->mReadonly);

    // Check host binds

    std::vector<std::string> expectedBindings = {"/etc/nsswitch.conf", "/etc/ssl"};

    for (const auto& bind : expectedBindings) {
        EXPECT_TRUE(CheckMount(*runtimeConfig, Mount {bind.c_str(), bind.c_str(), "bind", "bind,ro"}).IsNone());
    }

    // Check Aos env vars

    EXPECT_TRUE(CheckEnvVar(*runtimeConfig, "AOS_ITEM_ID=" + std::string(instance.mItemID.CStr())).IsNone());
    EXPECT_TRUE(CheckEnvVar(*runtimeConfig, "AOS_SUBJECT_ID=" + std::string(instance.mSubjectID.CStr())).IsNone());
    EXPECT_TRUE(CheckEnvVar(*runtimeConfig, "AOS_INSTANCE_INDEX=" + std::to_string(instance.mInstance)).IsNone());
    EXPECT_TRUE(CheckEnvVar(*runtimeConfig, "AOS_INSTANCE_ID=" + instanceID).IsNone());
}

TEST_F(ContainerRuntimeTest, ImageConfig)
{
    InstanceInfo instance;

    instance.mItemID    = "item0";
    instance.mSubjectID = "subject0";
    instance.mInstance  = 0;

    auto instanceID    = CreateInstanceID(static_cast<const InstanceIdent&>(instance));
    auto status        = std::make_unique<InstanceStatus>();
    auto runtimeConfig = std::make_unique<oci::RuntimeConfig>();
    auto imageConfig   = std::make_unique<oci::ImageConfig>();

    EXPECT_CALL(mOCISpecMock, LoadImageConfig(_, _))
        .WillOnce(Invoke([&imageConfig](const String&, oci::ImageConfig& config) {
            imageConfig->mConfig.mEnv.EmplaceBack("ENV_VAR1=value1");
            imageConfig->mConfig.mEnv.EmplaceBack("ENV_VAR2=value2");
            imageConfig->mConfig.mEnv.EmplaceBack("ENV_VAR3=value3");
            imageConfig->mConfig.mEntryPoint.EmplaceBack("/bin/example1");
            imageConfig->mConfig.mEntryPoint.EmplaceBack("/bin/example2");
            imageConfig->mConfig.mCmd.EmplaceBack("arg1");
            imageConfig->mConfig.mCmd.EmplaceBack("arg2");
            imageConfig->mConfig.mCmd.EmplaceBack("arg3");
            imageConfig->mConfig.mWorkingDir = "/work/dir";

            config = *imageConfig;

            return ErrorEnum::eNone;
        }));
    EXPECT_CALL(mOCISpecMock, SaveRuntimeConfig(_, _))
        .WillOnce(Invoke([&runtimeConfig](const String&, const oci::RuntimeConfig& config) {
            *runtimeConfig = config;

            return ErrorEnum::eNone;
        }));

    auto err = mRuntime.StartInstance(instance, *status);
    ASSERT_TRUE(err.IsNone()) << "Failed to start instance: " << tests::utils::ErrorToStr(err);

    // Check args

    auto expectedArgs = std::make_unique<StaticArray<StaticString<oci::cMaxParamLen>, oci::cMaxParamCount>>();

    for (const auto& arg : imageConfig->mConfig.mEntryPoint) {
        expectedArgs->PushBack(arg);
    }

    for (const auto& arg : imageConfig->mConfig.mCmd) {
        expectedArgs->PushBack(arg);
    }

    EXPECT_EQ(runtimeConfig->mProcess->mArgs, *expectedArgs);

    // Check image config env vars

    EXPECT_TRUE(CheckEnvVar(*runtimeConfig, "ENV_VAR1=value1").IsNone());
    EXPECT_TRUE(CheckEnvVar(*runtimeConfig, "ENV_VAR2=value2").IsNone());
    EXPECT_TRUE(CheckEnvVar(*runtimeConfig, "ENV_VAR3=value3").IsNone());
}

TEST_F(ContainerRuntimeTest, ServiceConfig)
{
    InstanceInfo instance;

    instance.mItemID    = "item0";
    instance.mSubjectID = "subject0";
    instance.mInstance  = 0;

    auto instanceID    = CreateInstanceID(static_cast<const InstanceIdent&>(instance));
    auto status        = std::make_unique<InstanceStatus>();
    auto runtimeConfig = std::make_unique<oci::RuntimeConfig>();
    auto serviceConfig = std::make_unique<oci::ServiceConfig>();

    EXPECT_CALL(mOCISpecMock, LoadImageManifest(_, _)).WillOnce(Invoke([](const String&, oci::ImageManifest& manifest) {
        manifest.mAosService.EmplaceValue();

        return ErrorEnum::eNone;
    }));
    EXPECT_CALL(mOCISpecMock, LoadServiceConfig(_, _))
        .WillOnce(Invoke([&serviceConfig](const String&, oci::ServiceConfig& config) {
            serviceConfig->mHostname.SetValue("example-host");
            serviceConfig->mSysctl.Emplace("net.ipv4.ip_forward", "1");
            serviceConfig->mSysctl.Emplace("net.ipv4.conf.all.rp_filter", "1");
            serviceConfig->mSysctl.Emplace("net.ipv4.conf.default.rp_filter", "1");

            serviceConfig->mQuotas.mCPUDMIPSLimit = 5000;
            serviceConfig->mQuotas.mRAMLimit      = 256 * 1024 * 1024;
            serviceConfig->mQuotas.mPIDsLimit     = 100;
            serviceConfig->mQuotas.mNoFileLimit   = 2048;
            serviceConfig->mQuotas.mTmpLimit      = 512 * 1024 * 1024;

            serviceConfig->mPermissions.EmplaceBack(FunctionServicePermissions {"kuksa", {}});

            config = *serviceConfig;

            return ErrorEnum::eNone;
        }));
    EXPECT_CALL(mPermHandlerMock, RegisterInstance(_, _))
        .WillOnce(Return(RetWithError<StaticString<cSecretLen>> {"instance-secret"}));
    EXPECT_CALL(mOCISpecMock, SaveRuntimeConfig(_, _))
        .WillOnce(Invoke([&runtimeConfig](const String&, const oci::RuntimeConfig& config) {
            *runtimeConfig = config;

            return ErrorEnum::eNone;
        }));

    auto err = mRuntime.StartInstance(instance, *status);
    ASSERT_TRUE(err.IsNone()) << "Failed to start instance: " << tests::utils::ErrorToStr(err);

    // Check hostname

    EXPECT_EQ(runtimeConfig->mHostname, *serviceConfig->mHostname);

    // Check sysctl

    EXPECT_EQ(runtimeConfig->mLinux->mSysctl, serviceConfig->mSysctl);

    // Check CPU quota

    ASSERT_TRUE(runtimeConfig->mLinux->mResources.HasValue());
    ASSERT_TRUE(runtimeConfig->mLinux->mResources->mCPU.HasValue());
    EXPECT_EQ(*runtimeConfig->mLinux->mResources->mCPU->mQuota,
        100000 * mNodeInfo.mCPUs[0].mNumCores * (*serviceConfig->mQuotas.mCPUDMIPSLimit) / mNodeInfo.mMaxDMIPS);
    EXPECT_EQ(runtimeConfig->mLinux->mResources->mCPU->mPeriod, 100000);

    // Check memory quota

    ASSERT_TRUE(runtimeConfig->mLinux->mResources->mMemory.HasValue());
    EXPECT_EQ(runtimeConfig->mLinux->mResources->mMemory->mLimit, *serviceConfig->mQuotas.mRAMLimit);

    // Check PID limit

    ASSERT_TRUE(runtimeConfig->mLinux->mResources->mPids.HasValue());
    EXPECT_EQ(runtimeConfig->mLinux->mResources->mPids->mLimit, *serviceConfig->mQuotas.mPIDsLimit);

    EXPECT_TRUE(CheckRLimits(*runtimeConfig,
        oci::POSIXRlimit {"RLIMIT_NPROC", *serviceConfig->mQuotas.mPIDsLimit, *serviceConfig->mQuotas.mPIDsLimit})
                    .IsNone());

    // Check NoFile limit

    EXPECT_TRUE(CheckRLimits(*runtimeConfig,
        oci::POSIXRlimit {"RLIMIT_NOFILE", *serviceConfig->mQuotas.mNoFileLimit, *serviceConfig->mQuotas.mNoFileLimit})
                    .IsNone());

    // Check /tmp limit

    EXPECT_TRUE(CheckMount(*runtimeConfig,
        Mount {"tmpfs", "/tmp", "tmpfs",
            ("nosuid,strictatime,mode=1777,size=" + std::to_string(*serviceConfig->mQuotas.mTmpLimit)).c_str()})
                    .IsNone());

    // Check permissions registration

    EXPECT_TRUE(CheckEnvVar(*runtimeConfig, "AOS_SECRET=instance-secret").IsNone());
}

TEST_F(ContainerRuntimeTest, Network)
{
    InstanceInfo instance;

    instance.mItemID   = "item0";
    instance.mInstance = 0;
    instance.mNetworkParameters.EmplaceValue();

    auto status        = std::make_unique<InstanceStatus>();
    auto runtimeConfig = std::make_unique<oci::RuntimeConfig>();

    EXPECT_CALL(mNetworkManagerMock, GetNetnsPath(_))
        .WillOnce(Return(RetWithError<StaticString<cFilePathLen>> {"/netns/path"}));
    EXPECT_CALL(mOCISpecMock, SaveRuntimeConfig(_, _))
        .WillOnce(Invoke([&runtimeConfig](const String&, const oci::RuntimeConfig& config) {
            *runtimeConfig = config;

            return ErrorEnum::eNone;
        }));

    auto err = mRuntime.StartInstance(instance, *status);
    ASSERT_TRUE(err.IsNone()) << "Failed to start instance: " << tests::utils::ErrorToStr(err);

    // Check netns

    EXPECT_TRUE(CheckNameSpace(*runtimeConfig, oci::LinuxNamespace {oci::LinuxNamespaceEnum::eNetwork, "/netns/path"})
                    .IsNone());
}

} // namespace aos::sm::launcher
