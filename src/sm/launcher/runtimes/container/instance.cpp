/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>

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

Instance::Instance(const InstanceInfo& instance, const ContainerConfig& config, FileSystemItf& fileSystem,
    RunnerItf& runner, imagemanager::ItemInfoProviderItf& itemInfoProvider,
    networkmanager::NetworkManagerItf& networkManager, oci::OCISpecItf& ociSpec)
    : mInstanceInfo(instance)
    , mConfig(config)
    , mFileSystem(fileSystem)
    , mRunner(runner)
    , mItemInfoProvider(itemInfoProvider)
    , mNetworkManager(networkManager)
    , mOCISpec(ociSpec)
{
    GenerateInstanceID();

    LOG_DBG() << "Create instance" << Log::Field("instance", mInstanceInfo)
              << Log::Field("instanceID", mInstanceID.c_str());
}

Instance::Instance(const std::string& instanceID, const ContainerConfig& config, FileSystemItf& fileSystem,
    RunnerItf& runner, imagemanager::ItemInfoProviderItf& itemInfoProvider,
    networkmanager::NetworkManagerItf& networkManager, oci::OCISpecItf& ociSpec)
    : mInstanceID(instanceID)
    , mConfig(config)
    , mFileSystem(fileSystem)
    , mRunner(runner)
    , mItemInfoProvider(itemInfoProvider)
    , mNetworkManager(networkManager)
    , mOCISpec(ociSpec)
{
    LOG_DBG() << "Create instance" << Log::Field("instanceID", mInstanceID.c_str());
}

Error Instance::Start()
{
    std::lock_guard lock {mMutex};

    auto runtimeDir = common::utils::JoinPath(mConfig.mRuntimeDir, mInstanceID);

    if (auto err = mFileSystem.ClearDir(runtimeDir); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    auto imageConfig   = std::make_unique<oci::ImageConfig>();
    auto serviceConfig = std::make_unique<oci::ServiceConfig>();
    auto runtimeConfig = std::make_unique<oci::RuntimeConfig>();

    if (auto err = LoadConfigs(*imageConfig, *serviceConfig); !err.IsNone()) {
        return err;
    }

    if (auto err = CreateRuntimeConfig(runtimeDir, *imageConfig, *serviceConfig, *runtimeConfig); !err.IsNone()) {
        return err;
    }

    mRunStatus = mRunner.StartInstance(mInstanceID, serviceConfig->mRunParameters);

    if (!mRunStatus.mError.IsNone()) {
        return AOS_ERROR_WRAP(mRunStatus.mError);
    }

    return ErrorEnum::eNone;
}

Error Instance::Stop()
{
    std::lock_guard lock {mMutex};

    Error stopErr;

    auto runtimeDir = common::utils::JoinPath(mConfig.mRuntimeDir, mInstanceID);

    if (auto err = mRunner.StopInstance(mInstanceID); !err.IsNone() && stopErr.IsNone()) {
        stopErr = AOS_ERROR_WRAP(err);
    }

    if (auto err = mFileSystem.RemoveAll(runtimeDir); !err.IsNone()) {
        stopErr = AOS_ERROR_WRAP(err);
    }

    mRunStatus.mInstanceID = mInstanceID;
    mRunStatus.mState      = InstanceStateEnum::eInactive;
    mRunStatus.mError      = stopErr;

    return stopErr;
}

void Instance::GetStatus(InstanceStatus& status) const
{
    std::lock_guard lock {mMutex};

    static_cast<InstanceIdent&>(status) = static_cast<const InstanceIdent&>(mInstanceInfo);
    status.mPreinstalled                = false;
    status.mRuntimeID                   = mInstanceInfo.mRuntimeID;
    status.mManifestDigest              = mInstanceInfo.mManifestDigest;
    status.mState                       = mRunStatus.mState;
    status.mError                       = mRunStatus.mError;
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
    (void)serviceConfig;

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

} // namespace aos::sm::launcher
