/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <numeric>

#include <core/common/tools/logger.hpp>

#include <common/utils/filesystem.hpp>
#include <common/utils/utils.hpp>

#include "instance.hpp"
#include "runtimeconfig.hpp"

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
    FileSystemItf& fileSystem, RunnerItf& runner, imagemanager::ItemInfoProviderItf& itemInfoProvider,
    networkmanager::NetworkManagerItf& networkManager, iamclient::PermHandlerItf& permHandler,
    resourcemanager::ResourceInfoProviderItf& resourceInfoProvider, oci::OCISpecItf& ociSpec)
    : mInstanceInfo(instance)
    , mConfig(config)
    , mNodeInfo(nodeInfo)
    , mFileSystem(fileSystem)
    , mRunner(runner)
    , mItemInfoProvider(itemInfoProvider)
    , mNetworkManager(networkManager)
    , mPermHandler(permHandler)
    , mResourceInfoProvider(resourceInfoProvider)
    , mOCISpec(ociSpec)
{
    GenerateInstanceID();

    LOG_DBG() << "Create instance" << Log::Field("instance", mInstanceInfo)
              << Log::Field("instanceID", mInstanceID.c_str());
}

Instance::Instance(const std::string& instanceID, const ContainerConfig& config, const NodeInfo& nodeInfo,
    FileSystemItf& fileSystem, RunnerItf& runner, imagemanager::ItemInfoProviderItf& itemInfoProvider,
    networkmanager::NetworkManagerItf& networkManager, iamclient::PermHandlerItf& permHandler,
    resourcemanager::ResourceInfoProviderItf& resourceInfoProvider, oci::OCISpecItf& ociSpec)
    : mInstanceID(instanceID)
    , mConfig(config)
    , mNodeInfo(nodeInfo)
    , mFileSystem(fileSystem)
    , mRunner(runner)
    , mItemInfoProvider(itemInfoProvider)
    , mNetworkManager(networkManager)
    , mPermHandler(permHandler)
    , mResourceInfoProvider(resourceInfoProvider)
    , mOCISpec(ociSpec)
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
    auto serviceConfig = std::make_unique<oci::ServiceConfig>();
    auto runtimeConfig = std::make_unique<oci::RuntimeConfig>();

    if (err = LoadConfigs(*imageConfig, *serviceConfig); !err.IsNone()) {
        return err;
    }

    if (err = CreateRuntimeConfig(runtimeDir, *imageConfig, *serviceConfig, *runtimeConfig); !err.IsNone()) {
        return err;
    }

    if (err = PrepareStateStorage(); !err.IsNone()) {
        return err;
    }

    if (err = PrepareRootFS(runtimeDir, *imageConfig, *runtimeConfig); !err.IsNone()) {
        return err;
    }

    if (mInstanceInfo.mNetworkParameters.HasValue()) {
        if (err = SetupNetwork(runtimeDir, *serviceConfig); !err.IsNone()) {
            return err;
        }
    }

    mRunStatus = mRunner.StartInstance(mInstanceID, serviceConfig->mRunParameters);
    err        = mRunStatus.mError;

    if (!err.IsNone()) {
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

    if (mInstanceInfo.mNetworkParameters.HasValue()) {
        if (auto err = mNetworkManager.RemoveInstanceFromNetwork(mInstanceID.c_str(), mInstanceInfo.mOwnerID);
            !err.IsNone() && stopErr.IsNone()) {
            stopErr = err;
        }
    }

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

void Instance::GenerateInstanceID()
{
    auto idStr = std::string(mInstanceInfo.mItemID.CStr()) + ":" + std::string(mInstanceInfo.mSubjectID.CStr()) + ":"
        + std::to_string(mInstanceInfo.mInstance);

    mInstanceID = common::utils::NameUUID(idStr);
}

Error Instance::LoadConfigs(oci::ImageConfig& imageConfig, oci::ServiceConfig& serviceConfig)
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

    if (manifest->mAosService.HasValue()) {
        if (auto err = mItemInfoProvider.GetBlobPath(manifest->mAosService->mDigest, *path); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (auto err = mOCISpec.LoadServiceConfig(*path, serviceConfig); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    return ErrorEnum::eNone;
}

Error Instance::CreateRuntimeConfig(const std::string& runtimeDir, const oci::ImageConfig& imageConfig,
    const oci::ServiceConfig& serviceConfig, oci::RuntimeConfig& runtimeConfig)
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

    if (mInstanceInfo.mNetworkParameters.HasValue()) {
        auto [instanceNetns, err] = mNetworkManager.GetNetnsPath(mInstanceID.c_str());
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (err = AddNamespace(oci::LinuxNamespace {oci::LinuxNamespaceEnum::eNetwork, instanceNetns}, runtimeConfig);
            !err.IsNone()) {
            return err;
        }
    }

    if (auto err = CreateAosEnvVars(runtimeConfig); !err.IsNone()) {
        return err;
    }

    if (auto err = ApplyImageConfig(imageConfig, runtimeConfig); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = ApplyServiceConfig(serviceConfig, runtimeConfig); !err.IsNone()) {
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

Error Instance::ApplyServiceConfig(const oci::ServiceConfig& serviceConfig, oci::RuntimeConfig& runtimeConfig)
{
    if (serviceConfig.mHostname.HasValue()) {
        runtimeConfig.mHostname = *serviceConfig.mHostname;
    }

    runtimeConfig.mLinux->mSysctl = serviceConfig.mSysctl;

    if (serviceConfig.mQuotas.mCPUDMIPSLimit.HasValue()) {
        int64_t quota
            = *serviceConfig.mQuotas.mCPUDMIPSLimit * cDefaultCPUPeriod * GetNumCPUCores() / mNodeInfo.mMaxDMIPS;
        if (quota < cMinCPUQuota) {
            quota = cMinCPUQuota;
        }

        if (auto err = SetCPULimit(quota, cDefaultCPUPeriod, runtimeConfig); !err.IsNone()) {
            return err;
        }
    }

    if (serviceConfig.mQuotas.mRAMLimit.HasValue()) {
        if (auto err = SetRAMLimit(*serviceConfig.mQuotas.mRAMLimit, runtimeConfig); !err.IsNone()) {
            return err;
        }
    }

    if (serviceConfig.mQuotas.mPIDsLimit.HasValue()) {
        auto pidLimit = *serviceConfig.mQuotas.mPIDsLimit;

        if (auto err = SetPIDLimit(pidLimit, runtimeConfig); !err.IsNone()) {
            return err;
        }

        if (auto err = AddRLimit(oci::POSIXRlimit {"RLIMIT_NPROC", pidLimit, pidLimit}, runtimeConfig); !err.IsNone()) {
            return err;
        }
    }

    if (serviceConfig.mQuotas.mNoFileLimit.HasValue()) {
        auto noFileLimit = *serviceConfig.mQuotas.mNoFileLimit;

        if (auto err = AddRLimit(oci::POSIXRlimit {"RLIMIT_NOFILE", noFileLimit, noFileLimit}, runtimeConfig);
            !err.IsNone()) {
            return err;
        }
    }

    if (serviceConfig.mQuotas.mTmpLimit.HasValue()) {
        StaticString<cFSMountOptionLen> tmpFSOpts;

        if (auto err = tmpFSOpts.Format("nosuid,strictatime,mode=1777,size=%lu", *serviceConfig.mQuotas.mTmpLimit);
            !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        auto mount = std::make_unique<Mount>("tmpfs", "/tmp", "tmpfs", tmpFSOpts);

        if (auto err = AddMount(*mount, runtimeConfig); !err.IsNone()) {
            return err;
        }
    }

    if (!serviceConfig.mPermissions.IsEmpty()) {
        auto [secret, err] = mPermHandler.RegisterInstance(mInstanceInfo, serviceConfig.mPermissions);
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

    if (auto err = AddResources(serviceConfig.mResources, runtimeConfig); !err.IsNone()) {
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

Error Instance::AddResources(const Array<StaticString<cResourceNameLen>>& resources, oci::RuntimeConfig& runtimeConfig)
{
    for (const auto& resource : resources) {
        auto resourceInfo = std::make_unique<sm::resourcemanager::ResourceInfo>();

        if (auto err = mResourceInfoProvider.GetResourceInfo(resource, *resourceInfo); !err.IsNone()) {
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

        if (auto err = AddDevices(resourceInfo->mDevices, runtimeConfig); !err.IsNone()) {
            return err;
        }
    }

    return ErrorEnum::eNone;
}

Error Instance::AddDevices(const Array<StaticString<cDeviceNameLen>>& devices, oci::RuntimeConfig& runtimeConfig)
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

        StaticString<cPermissionsLen> permissions;

        if (deviceParts->Size() == 3) {
            permissions = (*deviceParts)[2];
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

Error Instance::SetupNetwork(const std::string& runtimeDir, const oci::ServiceConfig& serviceConfig)
{
    LOG_DBG() << "Setup network" << Log::Field("instanceID", mInstanceID.c_str());

    auto networkParams = std::make_unique<networkmanager::InstanceNetworkParameters>();

    networkParams->mInstanceIdent = mInstanceInfo;

    auto etcDir = common::utils::JoinPath(runtimeDir, cMountPointsDir, "etc");

    if (auto err = networkParams->mHostsFilePath.Assign(common::utils::JoinPath(etcDir, "hosts").c_str());
        !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = networkParams->mResolvConfFilePath.Assign(common::utils::JoinPath(etcDir, "resolv.conf").c_str());
        !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    auto hosts = mConfig.mHosts;

    for (const auto& resource : serviceConfig.mResources) {
        if (auto err = AddNetworkHostsFromResource(resource.CStr(), hosts); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    for (const auto& host : hosts) {
        if (auto err = networkParams->mHosts.PushBack(host); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    networkParams->mNetworkParameters = *mInstanceInfo.mNetworkParameters;

    if (serviceConfig.mHostname.HasValue()) {
        networkParams->mHostname = *serviceConfig.mHostname;
    }

    if (serviceConfig.mQuotas.mDownloadSpeed.HasValue()) {
        networkParams->mIngressKbit = *serviceConfig.mQuotas.mDownloadSpeed;
    }

    if (serviceConfig.mQuotas.mUploadSpeed.HasValue()) {
        networkParams->mEgressKbit = *serviceConfig.mQuotas.mUploadSpeed;
    }

    if (serviceConfig.mQuotas.mDownloadLimit.HasValue()) {
        networkParams->mDownloadLimit = *serviceConfig.mQuotas.mDownloadLimit;
    }

    if (serviceConfig.mQuotas.mUploadLimit.HasValue()) {
        networkParams->mUploadLimit = *serviceConfig.mQuotas.mUploadLimit;
    }

    if (auto err = mFileSystem.PrepareNetworkDir(common::utils::JoinPath(runtimeDir, cMountPointsDir)); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = mNetworkManager.AddInstanceToNetwork(mInstanceID.c_str(), mInstanceInfo.mOwnerID, *networkParams);
        !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error Instance::AddNetworkHostsFromResource(const std::string& resource, std::vector<Host>& hosts)
{
    auto resourceInfo = std::make_unique<resourcemanager::ResourceInfo>();

    if (auto err = mResourceInfoProvider.GetResourceInfo(resource.c_str(), *resourceInfo); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    hosts.insert(hosts.end(), resourceInfo->mHosts.begin(), resourceInfo->mHosts.end());

    return ErrorEnum::eNone;
}

} // namespace aos::sm::launcher
