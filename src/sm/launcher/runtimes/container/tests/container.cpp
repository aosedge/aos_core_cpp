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
#include <core/sm/tests/mocks/launchermock.hpp>
#include <core/sm/tests/mocks/networkmanagermock.hpp>
#include <core/sm/tests/mocks/resourcemanagermock.hpp>

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

Error CheckAdditionalGID(const oci::RuntimeConfig& runtimeConfig, gid_t gid)
{
    if (auto it = std::find(runtimeConfig.mProcess->mUser.mAdditionalGIDs.begin(),
            runtimeConfig.mProcess->mUser.mAdditionalGIDs.end(), gid);
        it == runtimeConfig.mProcess->mUser.mAdditionalGIDs.end()) {
        return ErrorEnum::eNotFound;
    }

    return ErrorEnum::eNone;
}

Error CheckLinuxDevice(
    const oci::RuntimeConfig& runtimeConfig, const oci::LinuxDevice& device, const std::string& permissions)
{
    if (auto it = std::find(runtimeConfig.mLinux->mDevices.begin(), runtimeConfig.mLinux->mDevices.end(), device);
        it == runtimeConfig.mLinux->mDevices.end()) {
        return ErrorEnum::eNotFound;
    }

    auto it = std::find_if(runtimeConfig.mLinux->mResources->mDevices.begin(),
        runtimeConfig.mLinux->mResources->mDevices.end(), [&device](const oci::LinuxDeviceCgroup& cgroupDevice) {
            return cgroupDevice.mType == device.mType && cgroupDevice.mMajor == device.mMajor
                && cgroupDevice.mMinor == device.mMinor;
        });
    if (it == runtimeConfig.mLinux->mResources->mDevices.end()) {
        return ErrorEnum::eNotFound;
    }

    if (!it->mAllow) {
        return ErrorEnum::eFailed;
    }

    if (it->mAccess != permissions.c_str()) {
        return ErrorEnum::eFailed;
    }

    return ErrorEnum::eNone;
}

void CreateInstanceStatus(InstanceStatus& instanceStatus, const InstanceInfo& instanceInfo, InstanceStateEnum state,
    const Error& error = ErrorEnum::eNone)
{
    static_cast<InstanceIdent&>(instanceStatus) = instanceInfo;
    instanceStatus.mState                       = state;
    instanceStatus.mError                       = error;
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
        RuntimeConfig config = {"container", "runc", false, "/var/aos/workdir", nullptr};

        mNodeInfo = CreateNodeInfo();

        EXPECT_CALL(mCurrentNodeInfoProviderMock, GetCurrentNodeInfo(_))
            .WillRepeatedly(DoAll(SetArgReferee<0>(mNodeInfo), Return(ErrorEnum::eNone)));
        EXPECT_CALL(*mRuntime.mFileSystem, CreateHostFSWhiteouts(_, _)).WillOnce(Return(ErrorEnum::eNone));
        EXPECT_CALL(*mRuntime.mRunner, Init(_)).WillOnce(Invoke([&](RunStatusReceiverItf& runStatusReceiver) {
            mRunStatusReceiver = &runStatusReceiver;

            return ErrorEnum::eNone;
        }));

        auto err = mRuntime.Init(config, mCurrentNodeInfoProviderMock, mItemInfoProviderMock, mNetworkManagerMock,
            mPermHandlerMock, mResourceInfoProviderMock, mOCISpecMock, mInstanceStatusReceiverMock);
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

    TestRuntime                                         mRuntime;
    NodeInfo                                            mNodeInfo;
    NiceMock<iamclient::CurrentNodeInfoProviderMock>    mCurrentNodeInfoProviderMock;
    NiceMock<imagemanager::ItemInfoProviderMock>        mItemInfoProviderMock;
    NiceMock<networkmanager::NetworkManagerMock>        mNetworkManagerMock;
    NiceMock<iamclient::PermHandlerMock>                mPermHandlerMock;
    NiceMock<resourcemanager::ResourceInfoProviderMock> mResourceInfoProviderMock;
    NiceMock<oci::OCISpecMock>                          mOCISpecMock;
    NiceMock<InstanceStatusReceiverMock>                mInstanceStatusReceiverMock;
    RunStatusReceiverItf*                               mRunStatusReceiver {};
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

    auto receivedStatus1 = std::make_unique<InstanceStatus>();
    auto receivedStatus2 = std::make_unique<InstanceStatus>();

    CreateInstanceStatus(*receivedStatus1, instance, InstanceStateEnum::eActivating);
    CreateInstanceStatus(*receivedStatus2, instance, InstanceStateEnum::eActive);

    EXPECT_CALL(
        mInstanceStatusReceiverMock, OnInstancesStatusesReceived(Array<InstanceStatus>(receivedStatus1.get(), 1)))
        .Times(1);
    EXPECT_CALL(
        mInstanceStatusReceiverMock, OnInstancesStatusesReceived(Array<InstanceStatus>(receivedStatus2.get(), 1)))
        .Times(1);
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
    instance.mNetworkParameters.EmplaceValue();

    auto instanceID = CreateInstanceID(static_cast<const InstanceIdent&>(instance));
    auto status     = std::make_unique<InstanceStatus>();

    auto receivedStatus = std::make_unique<InstanceStatus>();

    CreateInstanceStatus(*receivedStatus, instance, InstanceStateEnum::eInactive);

    EXPECT_CALL(mOCISpecMock, LoadImageManifest(_, _)).WillOnce(Invoke([](const String&, oci::ImageManifest& manifest) {
        manifest.mAosService.EmplaceValue();

        return ErrorEnum::eNone;
    }));
    EXPECT_CALL(mOCISpecMock, LoadServiceConfig(_, _)).WillOnce(Invoke([](const String&, oci::ServiceConfig& config) {
        config.mPermissions.EmplaceBack(FunctionServicePermissions {"kuksa", {}});

        return ErrorEnum::eNone;
    }));
    EXPECT_CALL(mNetworkManagerMock, GetNetnsPath(_))
        .WillOnce(Return(RetWithError<StaticString<cFilePathLen>> {"/netns/path"}));
    EXPECT_CALL(mPermHandlerMock, RegisterInstance(_, _))
        .WillOnce(Return(RetWithError<StaticString<cSecretLen>> {"instance-secret"}));

    auto err = mRuntime.StartInstance(instance, *status);
    ASSERT_TRUE(err.IsNone()) << "Failed to start instance: " << tests::utils::ErrorToStr(err);

    EXPECT_CALL(*mRuntime.mRunner, StopInstance(instanceID)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mPermHandlerMock, UnregisterInstance(instance)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mNetworkManagerMock, RemoveInstanceFromNetwork(String(instanceID.c_str()), instance.mOwnerID))
        .WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mRuntime.mFileSystem, UmountServiceRootFS(_)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mRuntime.mFileSystem, RemoveAll(_)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(
        mInstanceStatusReceiverMock, OnInstancesStatusesReceived(Array<InstanceStatus>(receivedStatus.get(), 1)))
        .Times(1);

    err = mRuntime.StopInstance(static_cast<const InstanceIdent&>(instance), *status);
    ASSERT_TRUE(err.IsNone()) << "Failed to stop instance: " << tests::utils::ErrorToStr(err);

    EXPECT_EQ(status->mState, InstanceStateEnum::eInactive);

    // Stop the same instance again

    err = mRuntime.StopInstance(static_cast<const InstanceIdent&>(instance), *status);
    EXPECT_TRUE(err.Is(ErrorEnum::eNotFound)) << "Wrong error: " << tests::utils::ErrorToStr(err);
}

TEST_F(ContainerRuntimeTest, UpdateInstanceStatus)
{
    InstanceInfo instance;

    instance.mItemID    = "item0";
    instance.mSubjectID = "subject0";
    instance.mInstance  = 0;

    auto instanceID     = CreateInstanceID(static_cast<const InstanceIdent&>(instance));
    auto status         = std::make_unique<InstanceStatus>();
    auto receivedStatus = std::make_unique<InstanceStatus>();

    CreateInstanceStatus(*receivedStatus, instance, InstanceStateEnum::eFailed, ErrorEnum::eFailed);

    EXPECT_CALL(*mRuntime.mRunner, StartInstance(instanceID, _))
        .WillOnce(Return(RunStatus {"", InstanceStateEnum::eActive, ErrorEnum::eNone}));

    auto err = mRuntime.StartInstance(instance, *status);
    ASSERT_TRUE(err.IsNone()) << "Failed to start instance: " << tests::utils::ErrorToStr(err);

    // Check update status

    EXPECT_CALL(
        mInstanceStatusReceiverMock, OnInstancesStatusesReceived(Array<InstanceStatus>(receivedStatus.get(), 1)))
        .Times(1);

    mRunStatusReceiver->UpdateRunStatus(
        std::vector<RunStatus> {RunStatus {instanceID, InstanceStateEnum::eFailed, ErrorEnum::eFailed}});
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

    std::vector<resourcemanager::ResourceInfo> resourceInfos;

    resourceInfos.emplace_back();

    resourceInfos.back().mGroups.EmplaceBack("group1");
    resourceInfos.back().mGroups.EmplaceBack("group2");
    resourceInfos.back().mMounts.EmplaceBack(Mount {"/host/path1", "/container/path1", "bind", "ro"});
    resourceInfos.back().mMounts.EmplaceBack(Mount {"/host/path2", "/container/path2", "bind", "ro"});
    resourceInfos.back().mEnv.EmplaceBack("RESOURCE_ENV_VAR1=res_value1");
    resourceInfos.back().mEnv.EmplaceBack("RESOURCE_ENV_VAR2=res_value2");
    resourceInfos.back().mDevices.EmplaceBack("/dev/hostDevice1:/dev/containerDevice1:rw");

    resourceInfos.emplace_back();

    resourceInfos.back().mGroups.EmplaceBack("group3");
    resourceInfos.back().mGroups.EmplaceBack("group4");
    resourceInfos.back().mMounts.EmplaceBack(Mount {"/host/path3", "/container/path3", "bind", "ro"});
    resourceInfos.back().mMounts.EmplaceBack(Mount {"/host/path4", "/container/path4", "bind", "ro"});
    resourceInfos.back().mEnv.EmplaceBack("RESOURCE_ENV_VAR3=res_value3");
    resourceInfos.back().mEnv.EmplaceBack("RESOURCE_ENV_VAR4=res_value4");
    resourceInfos.back().mDevices.EmplaceBack("/dev/hostDevice2:/dev/containerDevice2:ro");

    std::vector<oci::LinuxDevice> ociLinuxDevices
        = {{"/dev/containerDevice1", "c", 1, 2, 3, 4, 5}, {"/dev/containerDevice2", "b", 6, 7, 8, 9, 10}};
    std::vector<std::string> devicePermissions = {"rw", "ro"};

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

            serviceConfig->mResources.EmplaceBack("resource1");
            serviceConfig->mResources.EmplaceBack("resource2");

            config = *serviceConfig;

            return ErrorEnum::eNone;
        }));
    EXPECT_CALL(mResourceInfoProviderMock, GetResourceInfo(_, _))
        .WillOnce(Invoke([&resourceInfos](const String&, resourcemanager::ResourceInfo& resourceInfo) {
            resourceInfo = resourceInfos[0];

            return ErrorEnum::eNone;
        }))
        .WillOnce(Invoke([&resourceInfos](const String&, resourcemanager::ResourceInfo& resourceInfo) {
            resourceInfo = resourceInfos[1];

            return ErrorEnum::eNone;
        }));
    EXPECT_CALL(*mRuntime.mFileSystem, GetGIDByName(_))
        .WillOnce(Return(RetWithError<gid_t> {1}))
        .WillOnce(Return(RetWithError<gid_t> {2}))
        .WillOnce(Return(RetWithError<gid_t> {3}))
        .WillOnce(Return(RetWithError<gid_t> {4}));
    EXPECT_CALL(*mRuntime.mFileSystem, PopulateHostDevices(_, _))
        .WillRepeatedly(Invoke([&ociLinuxDevices](const std::string& path, std::vector<oci::LinuxDevice>& ociDevices) {
            if (path == "/dev/hostDevice1") {
                ociDevices.push_back(ociLinuxDevices[0]);
            } else if (path == "/dev/hostDevice2") {
                ociDevices.push_back(ociLinuxDevices[1]);
            } else {
                return ErrorEnum::eNotFound;
            }

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

    // Check resources

    EXPECT_TRUE(CheckAdditionalGID(*runtimeConfig, 1).IsNone());
    EXPECT_TRUE(CheckAdditionalGID(*runtimeConfig, 2).IsNone());
    EXPECT_TRUE(CheckAdditionalGID(*runtimeConfig, 3).IsNone());
    EXPECT_TRUE(CheckAdditionalGID(*runtimeConfig, 4).IsNone());

    for (const auto& resourceInfo : resourceInfos) {
        for (const auto& mount : resourceInfo.mMounts) {
            EXPECT_TRUE(CheckMount(*runtimeConfig, mount).IsNone());
        }

        for (const auto& envVar : resourceInfo.mEnv) {
            EXPECT_TRUE(CheckEnvVar(*runtimeConfig, envVar.CStr()).IsNone());
        }
    }

    for (size_t i = 0; i < ociLinuxDevices.size(); ++i) {
        EXPECT_TRUE(CheckLinuxDevice(*runtimeConfig, ociLinuxDevices[i], devicePermissions[i]).IsNone());
    }
}

TEST_F(ContainerRuntimeTest, StorageState)
{
    InstanceInfo instance;

    instance.mItemID      = "item0";
    instance.mSubjectID   = "subject0";
    instance.mInstance    = 0;
    instance.mUID         = 1000;
    instance.mGID         = 1001;
    instance.mStatePath   = "state";
    instance.mStoragePath = "storage";

    auto status      = std::make_unique<InstanceStatus>();
    auto statePath   = "/var/aos/workdir/states/" + std::string(instance.mStatePath.CStr());
    auto storagePath = "/var/aos/workdir/storages/" + std::string(instance.mStoragePath.CStr());

    auto runtimeConfig = std::make_unique<oci::RuntimeConfig>();

    EXPECT_CALL(*mRuntime.mFileSystem, GetAbsPath(_)).WillRepeatedly(Invoke([](const std::string& path) {
        return RetWithError<std::string> {path};
    }));
    EXPECT_CALL(mOCISpecMock, SaveRuntimeConfig(_, _))
        .WillOnce(Invoke([&runtimeConfig](const String&, const oci::RuntimeConfig& config) {
            *runtimeConfig = config;

            return ErrorEnum::eNone;
        }));
    EXPECT_CALL(*mRuntime.mFileSystem, PrepareServiceState(statePath, 1000, 1001)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mRuntime.mFileSystem, PrepareServiceStorage(storagePath, 1000, 1001))
        .WillOnce(Return(ErrorEnum::eNone));

    auto err = mRuntime.StartInstance(instance, *status);
    ASSERT_TRUE(err.IsNone()) << "Failed to start instance: " << tests::utils::ErrorToStr(err);

    // Check state and storage

    EXPECT_TRUE(CheckMount(*runtimeConfig, Mount {(statePath).c_str(), "/state.dat", "bind", "bind,rw"}).IsNone());
    EXPECT_TRUE(CheckMount(*runtimeConfig, Mount {(storagePath).c_str(), "/storage", "bind", "bind,rw"}).IsNone());
}

TEST_F(ContainerRuntimeTest, OverrideEnvVars)
{
    InstanceInfo instance;

    instance.mItemID    = "item0";
    instance.mSubjectID = "subject0";
    instance.mInstance  = 0;
    instance.mEnvVars.EmplaceBack(EnvVar {"OVERRIDE_ENV_VAR1", "override_value1"});
    instance.mEnvVars.EmplaceBack(EnvVar {"OVERRIDE_ENV_VAR2", "override_value2"});
    instance.mEnvVars.EmplaceBack(EnvVar {"OVERRIDE_ENV_VAR3", "override_value3"});

    auto status        = std::make_unique<InstanceStatus>();
    auto runtimeConfig = std::make_unique<oci::RuntimeConfig>();

    EXPECT_CALL(mOCISpecMock, SaveRuntimeConfig(_, _))
        .WillOnce(Invoke([&runtimeConfig](const String&, const oci::RuntimeConfig& config) {
            *runtimeConfig = config;

            return ErrorEnum::eNone;
        }));

    auto err = mRuntime.StartInstance(instance, *status);
    ASSERT_TRUE(err.IsNone()) << "Failed to start instance: " << tests::utils::ErrorToStr(err);

    // Check overridden env vars

    EXPECT_TRUE(CheckEnvVar(*runtimeConfig, "OVERRIDE_ENV_VAR1=override_value1").IsNone());
    EXPECT_TRUE(CheckEnvVar(*runtimeConfig, "OVERRIDE_ENV_VAR2=override_value2").IsNone());
    EXPECT_TRUE(CheckEnvVar(*runtimeConfig, "OVERRIDE_ENV_VAR3=override_value3").IsNone());
}

TEST_F(ContainerRuntimeTest, Rootfs)
{
    InstanceInfo instance;

    instance.mItemID    = "item0";
    instance.mSubjectID = "subject0";
    instance.mInstance  = 0;

    auto instanceID    = CreateInstanceID(static_cast<const InstanceIdent&>(instance));
    auto status        = std::make_unique<InstanceStatus>();
    auto runtimeConfig = std::make_unique<oci::RuntimeConfig>();

    std::vector<Mount> expectedMounts = {
        Mount {"proc", "/proc", "proc"},
        Mount {"tmpfs", "/dev", "tmpfs", "nosuid,strictatime,mode=755,size=65536k"},
        Mount {"devpts", "/dev/pts", "devpts", "nosuid,noexec,newinstance,ptmxmode=0666,mode=0620,gid=5"},
        Mount {"shm", "/dev/shm", "tmpfs", "nosuid,noexec,nodev,mode=1777,size=65536k"},
        Mount {"mqueue", "/dev/mqueue", "mqueue", "nosuid,noexec,nodev"},
        Mount {"sysfs", "/sys", "sysfs", "nosuid,noexec,nodev,ro"},
        Mount {"cgroup", "/sys/fs/cgroup", "cgroup", "nosuid,noexec,nodev,relatime,ro"},
        Mount {"/etc/nsswitch.conf", "/etc/nsswitch.conf", "bind", "bind,ro"},
        Mount {"/etc/ssl", "/etc/ssl", "bind", "bind,ro"},
    };

    std::vector<std::string> expectedLayerPaths = {"/run/aos/runtime/" + instanceID + "/mounts",
        "/images/sha256/layer1", "/images/sha256/layer2", "/images/sha256/layer3", "/var/aos/workdir/whiteouts", "/"};

    EXPECT_CALL(mOCISpecMock, LoadImageConfig(_, _)).WillOnce(Invoke([](const String&, oci::ImageConfig& config) {
        config.mRootfs.mDiffIDs.EmplaceBack("sha256:layer1");
        config.mRootfs.mDiffIDs.EmplaceBack("sha256:layer2");
        config.mRootfs.mDiffIDs.EmplaceBack("sha256:layer3");

        return ErrorEnum::eNone;
    }));
    EXPECT_CALL(mOCISpecMock, SaveRuntimeConfig(_, _))
        .WillOnce(Invoke([&runtimeConfig](const String&, const oci::RuntimeConfig& config) {
            *runtimeConfig = config;

            return ErrorEnum::eNone;
        }));
    EXPECT_CALL(*mRuntime.mFileSystem, CreateMountPoints(_, expectedMounts)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mItemInfoProviderMock, GetLayerPath(_, _))
        .WillRepeatedly(Invoke([](const String& digest, String& path) {
            auto s = "/images/" + std::string(digest.CStr());

            std::replace(s.begin(), s.end(), ':', '/');

            path = s.c_str();

            return ErrorEnum::eNone;
        }));
    EXPECT_CALL(*mRuntime.mFileSystem, MountServiceRootFS(_, expectedLayerPaths)).WillOnce(Return(ErrorEnum::eNone));

    auto err = mRuntime.StartInstance(instance, *status);
    ASSERT_TRUE(err.IsNone()) << "Failed to start instance: " << tests::utils::ErrorToStr(err);
}

TEST_F(ContainerRuntimeTest, Network)
{
    InstanceInfo instance;

    instance.mItemID    = "item0";
    instance.mSubjectID = "subject0";
    instance.mInstance  = 0;
    instance.mOwnerID   = "owner0";
    instance.mNetworkParameters.EmplaceValue();
    instance.mNetworkParameters->mNetworkID = "network0";
    instance.mNetworkParameters->mSubnet    = "subnet0";
    instance.mNetworkParameters->mIP        = "192.168.1.100";

    auto instanceID = CreateInstanceID(static_cast<const InstanceIdent&>(instance));

    auto status        = std::make_unique<InstanceStatus>();
    auto runtimeConfig = std::make_unique<oci::RuntimeConfig>();
    auto networkParams = std::make_unique<networkmanager::InstanceNetworkParameters>();

    networkParams->mInstanceIdent      = instance;
    networkParams->mNetworkParameters  = *instance.mNetworkParameters;
    networkParams->mHostsFilePath      = ("/run/aos/runtime/" + instanceID + "/mounts/etc/hosts").c_str();
    networkParams->mResolvConfFilePath = ("/run/aos/runtime/" + instanceID + "/mounts/etc/resolv.conf").c_str();
    networkParams->mHostname           = "example-host";
    networkParams->mIngressKbit        = 1000;
    networkParams->mEgressKbit         = 1000;
    networkParams->mDownloadLimit      = 1024 * 1024;
    networkParams->mUploadLimit        = 1024 * 1024;
    networkParams->mHosts.EmplaceBack(Host {"192.168.1.1", "host1"});
    networkParams->mHosts.EmplaceBack(Host {"192.168.1.2", "host2"});
    networkParams->mHosts.EmplaceBack(Host {"192.168.1.3", "host3"});
    networkParams->mHosts.EmplaceBack(Host {"192.168.1.4", "host4"});

    EXPECT_CALL(mOCISpecMock, LoadImageManifest(_, _)).WillOnce(Invoke([](const String&, oci::ImageManifest& manifest) {
        manifest.mAosService.EmplaceValue();

        return ErrorEnum::eNone;
    }));
    EXPECT_CALL(mOCISpecMock, LoadServiceConfig(_, _))
        .WillOnce(Invoke([&networkParams](const String&, oci::ServiceConfig& config) {
            config.mHostname.SetValue(networkParams->mHostname);
            config.mResources.EmplaceBack("resource1");
            config.mResources.EmplaceBack("resource2");
            config.mQuotas.mUploadSpeed.SetValue(networkParams->mEgressKbit);
            config.mQuotas.mDownloadSpeed.SetValue(networkParams->mIngressKbit);
            config.mQuotas.mUploadLimit.SetValue(networkParams->mUploadLimit);
            config.mQuotas.mDownloadLimit.SetValue(networkParams->mDownloadLimit);

            return ErrorEnum::eNone;
        }));
    EXPECT_CALL(mNetworkManagerMock, GetNetnsPath(_))
        .WillOnce(Return(RetWithError<StaticString<cFilePathLen>> {"/netns/path"}));
    EXPECT_CALL(mOCISpecMock, SaveRuntimeConfig(_, _))
        .WillOnce(Invoke([&runtimeConfig](const String&, const oci::RuntimeConfig& config) {
            *runtimeConfig = config;

            return ErrorEnum::eNone;
        }));
    EXPECT_CALL(mResourceInfoProviderMock, GetResourceInfo(_, _))
        .WillRepeatedly(Invoke([](const String& resource, resourcemanager::ResourceInfo& resourceInfo) {
            if (resource == "resource1") {
                resourceInfo.mHosts.EmplaceBack(Host {"192.168.1.1", "host1"});
                resourceInfo.mHosts.EmplaceBack(Host {"192.168.1.2", "host2"});
            } else if (resource == "resource2") {
                resourceInfo.mHosts.EmplaceBack(Host {"192.168.1.3", "host3"});
                resourceInfo.mHosts.EmplaceBack(Host {"192.168.1.4", "host4"});
            } else {
                return ErrorEnum::eNotFound;
            }

            return ErrorEnum::eNone;
        }));
    EXPECT_CALL(*mRuntime.mFileSystem, PrepareNetworkDir(_)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(
        mNetworkManagerMock, AddInstanceToNetwork(String(instanceID.c_str()), instance.mOwnerID, *networkParams))
        .WillOnce(Return(ErrorEnum::eNone));

    auto err = mRuntime.StartInstance(instance, *status);
    ASSERT_TRUE(err.IsNone()) << "Failed to start instance: " << tests::utils::ErrorToStr(err);

    // Check netns

    EXPECT_TRUE(CheckNameSpace(*runtimeConfig, oci::LinuxNamespace {oci::LinuxNamespaceEnum::eNetwork, "/netns/path"})
                    .IsNone());
}

} // namespace aos::sm::launcher
