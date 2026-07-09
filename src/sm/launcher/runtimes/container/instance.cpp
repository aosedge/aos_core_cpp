/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <numeric>

#include <core/common/tools/logger.hpp>

#include <common/utils/exception.hpp>
#include <common/utils/filesystem.hpp>
#include <common/utils/utils.hpp>

#include "instance.hpp"
#include "runtimeconfig.hpp"

#define WITH_NETWORK 0

namespace aos::sm::launcher {

namespace {

/***********************************************************************************************************************
 * Consts
 **********************************************************************************************************************/

static const char* const cBindEtcEntries[] = {"nsswitch.conf", "ssl"};

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Instance::Instance(const InstanceInfo& instance, const ContainerConfig& config, const NodeInfo& nodeInfo,
    FileSystemItf& fileSystem, RunnerItf& runner, MonitoringItf& monitoring,
    imagemanager::ItemInfoProviderItf& itemInfoProvider, networkmanager::NetworkManagerItf& networkManager,
    iamclient::PermHandlerItf& permHandler, resourcemanager::ResourceInfoProviderItf& resourceInfoProvider,
    oci::OCISpecItf& ociSpec, InstanceIDProviderItf& instanceIDProvider)
    : mInstanceInfo(instance)
    , mConfig(config)
    , mNodeInfo(nodeInfo)
    , mFileSystem(fileSystem)
    , mRunner(runner)
    , mMonitoring(monitoring)
    , mItemInfoProvider(itemInfoProvider)
    , mNetworkManager(networkManager)
    , mPermHandler(permHandler)
    , mResourceInfoProvider(resourceInfoProvider)
    , mOCISpec(ociSpec)
    , mInstanceIDProvider(instanceIDProvider)
{
    StaticString<cIDLen> generatedID;

    auto err = mInstanceIDProvider.GetInstanceID(instance, generatedID);
    AOS_ERROR_CHECK_AND_THROW(err, "Failed to get instance ID");

    mInstanceID = generatedID.CStr();

    LOG_DBG() << "Create instance" << Log::Field("instance", mInstanceInfo)
              << Log::Field("instanceID", mInstanceID.c_str());
}

Instance::Instance(const std::string& instanceID, const ContainerConfig& config, const NodeInfo& nodeInfo,
    FileSystemItf& fileSystem, RunnerItf& runner, MonitoringItf& monitoring,
    imagemanager::ItemInfoProviderItf& itemInfoProvider, networkmanager::NetworkManagerItf& networkManager,
    iamclient::PermHandlerItf& permHandler, resourcemanager::ResourceInfoProviderItf& resourceInfoProvider,
    oci::OCISpecItf& ociSpec, InstanceIDProviderItf& instanceIDProvider)
    : mInstanceID(instanceID)
    , mConfig(config)
    , mNodeInfo(nodeInfo)
    , mFileSystem(fileSystem)
    , mRunner(runner)
    , mMonitoring(monitoring)
    , mItemInfoProvider(itemInfoProvider)
    , mNetworkManager(networkManager)
    , mPermHandler(permHandler)
    , mResourceInfoProvider(resourceInfoProvider)
    , mOCISpec(ociSpec)
    , mInstanceIDProvider(instanceIDProvider)
{
    LOG_DBG() << "Create instance" << Log::Field("instanceID", mInstanceID.c_str());
}

Error Instance::Start()
{
    std::lock_guard lock {mMutex};

    Error err;

    auto updateRunStatus = DeferRelease(&err, [&](const Error* err) {
        if (!err->IsNone() && mRunStatus.mState != InstanceStateEnum::eFailed) {
            mRunStatus.mState = InstanceStateEnum::eFailed;
            mRunStatus.mError = *err;
        }
    });

    auto runtimeDir = common::utils::JoinPath(mConfig.mRuntimeDir, mInstanceID);

    if (err = mFileSystem.ClearDir(runtimeDir); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    auto imageConfig   = std::make_unique<oci::ImageConfig>();
    auto itemConfig    = std::make_unique<oci::ItemConfig>();
    auto runtimeConfig = std::make_unique<oci::RuntimeConfig>();

    if (err = LoadConfigs(*imageConfig, *itemConfig); !err.IsNone()) {
        return err;
    }

    if (err = CreateRuntimeConfig(runtimeDir, *imageConfig, *itemConfig, *runtimeConfig); !err.IsNone()) {
        return err;
    }

    if (err = PrepareStateStorage(); !err.IsNone()) {
        return err;
    }

    if (err = PrepareRootFS(runtimeDir, *imageConfig, *runtimeConfig); !err.IsNone()) {
        return err;
    }

#if WITH_NETWORK
    if (err = StartNetwork(runtimeDir); !err.IsNone()) {
        return err;
    }
#endif

    if (mInstanceInfo.mMonitoringParams.HasValue()) {
        if (err = StartMonitoring(); !err.IsNone()) {
            return err;
        }
    }

    mRunStatus = mRunner.StartInstance(mInstanceID, itemConfig->mRunParameters);
    err        = mRunStatus.mError;

    if (mRunStatus.mState != InstanceStateEnum::eActive) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error Instance::Stop()
{
    std::lock_guard lock {mMutex};

    Error stopErr;

    auto updateRunStatus = DeferRelease(&stopErr, [&](const Error* err) {
        mRunStatus.mInstanceID = mInstanceID;
        mRunStatus.mState      = err->IsNone() ? InstanceStateEnum::eInactive : InstanceStateEnum::eFailed;
        mRunStatus.mError      = *err;
    });

    auto runtimeDir = common::utils::JoinPath(mConfig.mRuntimeDir, mInstanceID);

    if (auto err = mRunner.StopInstance(mInstanceID); !err.IsNone() && stopErr.IsNone()) {
        stopErr = AOS_ERROR_WRAP(err);
    }

    if (mPermissionsRegistered) {
        if (auto err = mPermHandler.UnregisterInstance(mInstanceInfo); !err.IsNone() && stopErr.IsNone()) {
            stopErr = err;
        }

        mPermissionsRegistered = false;
    }

    if (mInstanceInfo.mMonitoringParams.HasValue()) {
        if (auto err = mMonitoring.StopInstanceMonitoring(mInstanceID); !err.IsNone() && stopErr.IsNone()) {
            stopErr = AOS_ERROR_WRAP(err);
        }
    }

#if WITH_NETWORK
    if (auto err = mNetworkManager.StopInstanceNetwork(mInstanceID.c_str(), mInstanceInfo.mOwnerID);
        !err.IsNone() && stopErr.IsNone()) {
        stopErr = err;
    }
#endif

    auto rootfsPath = common::utils::JoinPath(runtimeDir, cRootFSDir);

    if (auto err = mFileSystem.UmountServiceRootFS(rootfsPath); !err.IsNone() && stopErr.IsNone()) {
        stopErr = AOS_ERROR_WRAP(err);
    }

    if (auto err = mFileSystem.RemoveAll(runtimeDir); !err.IsNone() && stopErr.IsNone()) {
        stopErr = AOS_ERROR_WRAP(err);
    }

    return stopErr;
}

void Instance::GetStatus(InstanceStatus& status) const
{
    std::lock_guard lock {mMutex};

    static_cast<InstanceIdent&>(status) = static_cast<const InstanceIdent&>(mInstanceInfo);
    status.mVersion                     = mInstanceInfo.mVersion;
    status.mPreinstalled                = false;
    status.mRuntimeID                   = mInstanceInfo.mRuntimeID;
    status.mManifestDigest              = mInstanceInfo.mManifestDigest;
    status.mState                       = mRunStatus.mState;
    status.mError                       = mRunStatus.mError;
}

bool Instance::UpdateRunStatus(const RunStatus& runStatus)
{
    std::lock_guard lock {mMutex};

    if (runStatus == mRunStatus) {
        return false;
    }

    mRunStatus = runStatus;

    return true;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

Error Instance::LoadConfigs(oci::ImageConfig& imageConfig, oci::ItemConfig& itemConfig)
{
    auto path = std::make_unique<StaticString<cFilePathLen>>();

    if (auto err = mItemInfoProvider.GetBlobPath(mInstanceInfo.mManifestDigest, *path); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    auto manifest = std::make_unique<oci::ImageManifest>();

    if (auto err = mOCISpec.LoadImageManifest(*path, *manifest); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = mItemInfoProvider.GetBlobPath(manifest->mConfig.mDigest, *path); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = mOCISpec.LoadImageConfig(*path, imageConfig); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (manifest->mItemConfig.HasValue()) {
        if (auto err = mItemInfoProvider.GetBlobPath(manifest->mItemConfig->mDigest, *path); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (auto err = mOCISpec.LoadItemConfig(*path, itemConfig); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    return ErrorEnum::eNone;
}

Error Instance::CreateRuntimeConfig(const std::string& runtimeDir, const oci::ImageConfig& imageConfig,
    const oci::ItemConfig& itemConfig, oci::RuntimeConfig& runtimeConfig)
{
    LOG_DBG() << "Create runtime config" << Log::Field("instanceID", mInstanceID.c_str());

    if (auto err = oci::CreateExampleRuntimeConfig(runtimeConfig); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    runtimeConfig.mProcess->mTerminal  = false;
    runtimeConfig.mProcess->mUser.mUID = mInstanceInfo.mUID;
    runtimeConfig.mProcess->mUser.mGID = mInstanceInfo.mGID;

    if (auto err
        = runtimeConfig.mLinux->mCgroupsPath.Assign(common::utils::JoinPath(cCgroupsPath, mInstanceID).c_str());
        !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = runtimeConfig.mRoot->mPath.Assign(common::utils::JoinPath(runtimeDir, cRootFSDir).c_str());
        !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    runtimeConfig.mRoot->mReadonly = false;

    if (auto err = BindHostDirs(runtimeConfig); !err.IsNone()) {
        return err;
    }

#if WITH_NETWORK
    auto [instanceNetns, netnsErr] = mNetworkManager.GetNetnsPath(mInstanceID.c_str());
    if (!netnsErr.IsNone()) {
        return AOS_ERROR_WRAP(netnsErr);
    }

    if (auto err = AddNamespace(oci::LinuxNamespace {oci::LinuxNamespaceEnum::eNetwork, instanceNetns}, runtimeConfig);
        !err.IsNone()) {
        return err;
    }
#endif

    if (auto err = CreateAosEnvVars(runtimeConfig); !err.IsNone()) {
        return err;
    }

    if (auto err = ApplyImageConfig(imageConfig, runtimeConfig); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = ApplyItemConfig(itemConfig, runtimeConfig); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = ApplyStateStorage(runtimeConfig); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = OverrideEnvVars(runtimeConfig); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err
        = mOCISpec.SaveRuntimeConfig(common::utils::JoinPath(runtimeDir, cRuntimeConfigFile).c_str(), runtimeConfig);
        !err.IsNone()) {
        return err;
    }

    return ErrorEnum::eNone;
}

Error Instance::BindHostDirs(oci::RuntimeConfig& runtimeConfig)
{
    for (const auto& hostEntry : cBindEtcEntries) {
        auto path  = common::utils::JoinPath("/etc", hostEntry);
        auto mount = std::make_unique<Mount>(path.c_str(), path.c_str(), "bind", "bind,ro");

        if (auto err = AddMount(*mount, runtimeConfig); !err.IsNone()) {
            return err;
        }
    }

    return ErrorEnum::eNone;
}

Error Instance::CreateAosEnvVars(oci::RuntimeConfig& runtimeConfig)
{
    auto                     envVars = std::make_unique<StaticArray<StaticString<cEnvVarLen>, cMaxNumEnvVariables>>();
    StaticString<cEnvVarLen> envVar;

    if (auto err = envVar.Format("%s=%s", cEnvAosItemID, mInstanceInfo.mItemID.CStr()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = envVars->PushBack(envVar); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = envVar.Format("%s=%s", cEnvAosSubjectID, mInstanceInfo.mSubjectID.CStr()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = envVars->PushBack(envVar); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = envVar.Format("%s=%d", cEnvAosInstanceIndex, mInstanceInfo.mInstance); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = envVars->PushBack(envVar); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = envVar.Format("%s=%s", cEnvAosInstanceID, mInstanceID.c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = envVars->PushBack(envVar); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = AddEnvVars(*envVars, runtimeConfig); !err.IsNone()) {
        return err;
    }

    return ErrorEnum::eNone;
}

Error Instance::ApplyImageConfig(const oci::ImageConfig& imageConfig, oci::RuntimeConfig& runtimeConfig)
{
    runtimeConfig.mProcess->mArgs.Clear();

    for (const auto& arg : imageConfig.mConfig.mEntryPoint) {
        if (auto err = runtimeConfig.mProcess->mArgs.PushBack(arg); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    for (const auto& arg : imageConfig.mConfig.mCmd) {
        if (auto err = runtimeConfig.mProcess->mArgs.PushBack(arg); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    runtimeConfig.mProcess->mCwd = imageConfig.mConfig.mWorkingDir;

    if (runtimeConfig.mProcess->mCwd.IsEmpty()) {
        runtimeConfig.mProcess->mCwd = "/";
    }

    if (auto err = AddEnvVars(imageConfig.mConfig.mEnv, runtimeConfig); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error Instance::ApplyItemConfig(const oci::ItemConfig& itemConfig, oci::RuntimeConfig& runtimeConfig)
{
    if (itemConfig.mHostname.HasValue()) {
        runtimeConfig.mHostname = *itemConfig.mHostname;
    }

    runtimeConfig.mLinux->mSysctl = itemConfig.mSysctl;

    if (itemConfig.mQuotas.mCPUDMIPSLimit.HasValue()) {
        int64_t quota = *itemConfig.mQuotas.mCPUDMIPSLimit * cDefaultCPUPeriod * GetNumCPUCores() / mNodeInfo.mMaxDMIPS;
        if (quota < cMinCPUQuota) {
            quota = cMinCPUQuota;
        }

        if (auto err = SetCPULimit(quota, cDefaultCPUPeriod, runtimeConfig); !err.IsNone()) {
            return err;
        }
    }

    if (itemConfig.mQuotas.mRAMLimit.HasValue()) {
        if (auto err = SetRAMLimit(*itemConfig.mQuotas.mRAMLimit, runtimeConfig); !err.IsNone()) {
            return err;
        }
    }

    if (itemConfig.mQuotas.mPIDsLimit.HasValue()) {
        auto pidLimit = *itemConfig.mQuotas.mPIDsLimit;

        if (auto err = SetPIDLimit(pidLimit, runtimeConfig); !err.IsNone()) {
            return err;
        }

        if (auto err = AddRLimit(oci::POSIXRlimit {"RLIMIT_NPROC", pidLimit, pidLimit}, runtimeConfig); !err.IsNone()) {
            return err;
        }
    }

    if (itemConfig.mQuotas.mNoFileLimit.HasValue()) {
        auto noFileLimit = *itemConfig.mQuotas.mNoFileLimit;

        if (auto err = AddRLimit(oci::POSIXRlimit {"RLIMIT_NOFILE", noFileLimit, noFileLimit}, runtimeConfig);
            !err.IsNone()) {
            return err;
        }
    }

    if (itemConfig.mQuotas.mTmpLimit.HasValue()) {
        StaticString<cFSMountOptionLen> tmpFSOpts;

        if (auto err = tmpFSOpts.Format("nosuid,strictatime,mode=1777,size=%lu", *itemConfig.mQuotas.mTmpLimit);
            !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        auto mount = std::make_unique<Mount>("tmpfs", "/tmp", "tmpfs", tmpFSOpts);

        if (auto err = AddMount(*mount, runtimeConfig); !err.IsNone()) {
            return err;
        }
    }

    if (!itemConfig.mPermissions.IsEmpty()) {
        auto [secret, err] = mPermHandler.RegisterInstance(mInstanceInfo, itemConfig.mPermissions);
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        mPermissionsRegistered = true;

        StaticString<cEnvVarLen> envVar;

        if (err = envVar.Format("%s=%s", cEnvAosSecret, secret.CStr()); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (err = AddEnvVars(Array<StaticString<cEnvVarLen>>(&envVar, 1), runtimeConfig); !err.IsNone()) {
            return err;
        }
    }

    if (auto err = AddResources(itemConfig.mResources, runtimeConfig); !err.IsNone()) {
        return err;
    }

    return ErrorEnum::eNone;
}

size_t Instance::GetNumCPUCores() const
{
    int numCores = std::accumulate(mNodeInfo.mCPUs.begin(), mNodeInfo.mCPUs.end(), 0,
        [](int sum, const auto& cpu) { return sum + cpu.mNumCores; });

    if (numCores == 0) {
        LOG_WRN() << "Can't identify number of CPU cores, default value (1) will be taken"
                  << Log::Field("instanceID", mInstanceID.c_str());

        numCores = 1;
    }

    return numCores;
}

Error Instance::AddResources(const Array<oci::ResourceInfo>& resources, oci::RuntimeConfig& runtimeConfig)
{
    for (const auto& resource : resources) {
        auto resourceInfo = std::make_unique<sm::resourcemanager::ResourceInfo>();

        if (auto err = mResourceInfoProvider.GetResourceInfo(resource.mName, *resourceInfo); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        for (const auto& group : resourceInfo->mGroups) {
            auto [gid, err] = mFileSystem.GetGIDByName(group.CStr());
            if (!err.IsNone()) {
                return AOS_ERROR_WRAP(err);
            }

            if (err = AddAdditionalGID(gid, runtimeConfig); !err.IsNone()) {
                return err;
            }
        }

        for (const auto& mount : resourceInfo->mMounts) {
            if (auto err = AddMount(mount, runtimeConfig); !err.IsNone()) {
                return err;
            }
        }

        if (auto err = AddEnvVars(resourceInfo->mEnv, runtimeConfig); !err.IsNone()) {
            return err;
        }

        if (auto err = AddDevices(resourceInfo->mDevices, resource.mMode, runtimeConfig); !err.IsNone()) {
            return err;
        }
    }

    return ErrorEnum::eNone;
}

Error Instance::AddDevices(
    const Array<StaticString<cDeviceNameLen>>& devices, const String& permissions, oci::RuntimeConfig& runtimeConfig)
{
    for (const auto& device : devices) {
        LOG_DBG() << "Set device" << Log::Field("instanceID", mInstanceID.c_str()) << Log::Field("device", device);

        auto deviceParts = std::make_unique<StaticArray<StaticString<cDeviceNameLen>, 3>>();

        if (auto err = device.Split(*deviceParts, ':'); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (deviceParts->IsEmpty()) {
            return AOS_ERROR_WRAP(Error(ErrorEnum::eInvalidArgument, "invalid device format"));
        }

        auto ociDevices = std::vector<oci::LinuxDevice>();

        if (auto err = mFileSystem.PopulateHostDevices((*deviceParts)[0].CStr(), ociDevices); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (deviceParts->Size() >= 2) {
            for (auto& ociDevice : ociDevices) {
                if (auto err = ociDevice.mPath.Replace((*deviceParts)[0], (*deviceParts)[1], 1); !err.IsNone()) {
                    return AOS_ERROR_WRAP(err);
                }
            }
        }

        for (const auto& ociDevice : ociDevices) {
            if (auto err = AddDevice(ociDevice, permissions, runtimeConfig); !err.IsNone()) {
                return err;
            }
        }
    }

    return ErrorEnum::eNone;
}

Error Instance::ApplyStateStorage(oci::RuntimeConfig& runtimeConfig)
{
    if (!mInstanceInfo.mStatePath.IsEmpty()) {
        auto [absPath, err]
            = mFileSystem.GetAbsPath(common::utils::JoinPath(mConfig.mStateDir, mInstanceInfo.mStatePath.CStr()));
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        auto mount = std::make_unique<Mount>(absPath.c_str(), cInstanceStateFile, "bind", "bind,rw");

        if (err = AddMount(*mount, runtimeConfig); !err.IsNone()) {
            return err;
        }
    }

    if (!mInstanceInfo.mStoragePath.IsEmpty()) {
        auto [absPath, err]
            = mFileSystem.GetAbsPath(common::utils::JoinPath(mConfig.mStorageDir, mInstanceInfo.mStoragePath.CStr()));
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        auto mount = std::make_unique<Mount>(absPath.c_str(), cInstanceStorageDir, "bind", "bind,rw");

        if (err = AddMount(*mount, runtimeConfig); !err.IsNone()) {
            return err;
        }
    }

    return ErrorEnum::eNone;
}

Error Instance::OverrideEnvVars(oci::RuntimeConfig& runtimeConfig)
{
    auto                     envVars = std::make_unique<StaticArray<StaticString<cEnvVarLen>, cMaxNumEnvVariables>>();
    StaticString<cEnvVarLen> envVar;

    for (const auto& overrideEnvVar : mInstanceInfo.mEnvVars) {
        if (auto err = envVar.Format("%s=%s", overrideEnvVar.mName.CStr(), overrideEnvVar.mValue.CStr());
            !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (auto err = envVars->PushBack(envVar); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    if (auto err = AddEnvVars(*envVars, runtimeConfig); !err.IsNone()) {
        return err;
    }

    return ErrorEnum::eNone;
}

Error Instance::PrepareStateStorage()
{
    if (!mInstanceInfo.mStatePath.IsEmpty()) {
        auto statePath = common::utils::JoinPath(mConfig.mStateDir, mInstanceInfo.mStatePath.CStr());

        LOG_DBG() << "Prepare state" << Log::Field("instanceID", mInstanceID.c_str())
                  << Log::Field("path", statePath.c_str());

        if (auto err = mFileSystem.PrepareServiceState(statePath, mInstanceInfo.mUID, mInstanceInfo.mGID);
            !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    if (!mInstanceInfo.mStoragePath.IsEmpty()) {
        auto storagePath = common::utils::JoinPath(mConfig.mStorageDir, mInstanceInfo.mStoragePath.CStr());

        LOG_DBG() << "Prepare storage" << Log::Field("instanceID", mInstanceID.c_str())
                  << Log::Field("path", storagePath.c_str());

        if (auto err = mFileSystem.PrepareServiceStorage(storagePath, mInstanceInfo.mUID, mInstanceInfo.mGID);
            !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    return ErrorEnum::eNone;
}

Error Instance::PrepareRootFS(
    const std::string& runtimeDir, const oci::ImageConfig& imageConfig, const oci::RuntimeConfig& runtimeConfig)
{
    LOG_DBG() << "Prepare rootfs" << Log::Field("instanceID", mInstanceID.c_str());

    auto mountPoints
        = std::vector<Mount>(runtimeConfig.mMounts.Get(), runtimeConfig.mMounts.Get() + runtimeConfig.mMounts.Size());
    auto mountPointsDir = common::utils::JoinPath(runtimeDir, cMountPointsDir);

    if (auto err = mFileSystem.CreateMountPoints(mountPointsDir, mountPoints); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    std::vector<std::string> layers;

    layers.push_back(mountPointsDir);

    for (const auto& layerDigest : imageConfig.mRootfs.mDiffIDs) {
        auto path = std::make_unique<StaticString<cFilePathLen>>();

        if (auto err = mItemInfoProvider.GetLayerPath(layerDigest, *path); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        layers.push_back(path->CStr());
    }

    layers.push_back(mConfig.mHostWhiteoutsDir);
    layers.push_back("/");

    if (auto err = mFileSystem.MountServiceRootFS(common::utils::JoinPath(runtimeDir, cRootFSDir), layers);
        !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error Instance::StartNetwork(const std::string& runtimeDir)
{
    LOG_DBG() << "Start network" << Log::Field("instanceID", mInstanceID.c_str());

    if (auto err = mFileSystem.PrepareNetworkDir(common::utils::JoinPath(runtimeDir, cMountPointsDir)); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    auto                                         etcDir = common::utils::JoinPath(runtimeDir, cMountPointsDir, "etc");
    networkmanager::InstanceNetworkRuntimeParams runtimeParams;

    if (auto err = runtimeParams.mHostsFilePath.Assign(common::utils::JoinPath(etcDir, "hosts").c_str());
        !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = runtimeParams.mResolvConfFilePath.Assign(common::utils::JoinPath(etcDir, "resolv.conf").c_str());
        !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = mNetworkManager.StartInstanceNetwork(mInstanceID.c_str(), mInstanceInfo.mOwnerID, runtimeParams);
        !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error Instance::StartMonitoring()
{
    std::vector<PartitionInfo> partInfos;

    if (!mInstanceInfo.mStoragePath.IsEmpty()) {
        auto partInfo = std::make_unique<PartitionInfo>();

        partInfo->mName = cStoragePartitionName;

        if (auto err = partInfo->mPath.Assign(
                common::utils::JoinPath(mConfig.mStorageDir, mInstanceInfo.mStoragePath.CStr()).c_str());
            !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (auto err = partInfo->mTypes.EmplaceBack(cStoragePartitionName); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        partInfos.push_back(*partInfo);
    }

    if (!mInstanceInfo.mStatePath.IsEmpty()) {
        auto partInfo = std::make_unique<PartitionInfo>();

        partInfo->mName = cStatePartitionName;

        if (auto err = partInfo->mPath.Assign(
                common::utils::JoinPath(mConfig.mStateDir, mInstanceInfo.mStatePath.CStr()).c_str());
            !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (auto err = partInfo->mTypes.EmplaceBack(cStatePartitionName); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        partInfos.push_back(*partInfo);
    }

    if (auto err = mMonitoring.StartInstanceMonitoring(mInstanceID, mInstanceInfo.mUID, partInfos); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::sm::launcher
