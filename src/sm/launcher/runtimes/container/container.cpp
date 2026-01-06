/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <core/common/tools/logger.hpp>

#include <common/utils/exception.hpp>
#include <common/utils/json.hpp>
#include <common/utils/utils.hpp>

#include "container.hpp"
#include "filesystem.hpp"
#include "runner.hpp"

namespace fs = std::filesystem;

namespace aos::sm::launcher {

namespace {

/***********************************************************************************************************************
 * Consts
 **********************************************************************************************************************/

const char* const cDefaultHostFSBinds[] = {"bin", "sbin", "lib", "lib64", "usr"};

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error ContainerRuntime::Init(const RuntimeConfig& config,
    iamclient::CurrentNodeInfoProviderItf& currentNodeInfoProvider, imagemanager::ItemInfoProviderItf& itemInfoProvider,
    oci::OCISpecItf& ociSpec) // cppcheck-suppress constParameterReference

{
    try {
        LOG_DBG() << "Init runtime" << Log::Field("type", config.mType.c_str());

        auto nodeInfo = std::make_unique<NodeInfo>();

        if (auto err = currentNodeInfoProvider.GetCurrentNodeInfo(*nodeInfo); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (auto err = CreateRuntimeInfo(config.mType, *nodeInfo); !err.IsNone()) {
            return err;
        }

        mRunner     = CreateRunner();
        mFileSystem = CreateFileSystem();

        mItemInfoProvider = &itemInfoProvider;
        mOCISpec          = &ociSpec;

        if (auto err = mRunner->Init(*this); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        ParseContainerConfig(config.mConfig
                ? common::utils::CaseInsensitiveObjectWrapper(config.mConfig)
                : common::utils::CaseInsensitiveObjectWrapper(Poco::makeShared<Poco::JSON::Object>()),
            config.mWorkingDir, mConfig);

        if (mConfig.mHostBinds.empty()) {
            for (const auto& bind : cDefaultHostFSBinds) {
                mConfig.mHostBinds.push_back(bind);
            }
        }

        if (auto err = mFileSystem->CreateHostFSWhiteouts(mConfig.mHostWhiteoutsDir, mConfig.mHostBinds);
            !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        return ErrorEnum::eNone;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }
}

Error ContainerRuntime::Start()
{
    try {
        LOG_DBG() << "Start runtime";

        if (auto err = mRunner->Start(); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (auto err = StopActiveInstances(); !err.IsNone()) {
            LOG_ERR() << "Failed to stop active instances" << Log::Field(err);
        }

        return ErrorEnum::eNone;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }
}

Error ContainerRuntime::Stop()
{
    try {
        LOG_DBG() << "Stop runtime";

        Error err;

        if (auto stopErr = mRunner->Stop(); !stopErr.IsNone()) {
            err = AOS_ERROR_WRAP(stopErr);
        }

        return err;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }
}

Error ContainerRuntime::GetRuntimeInfo(RuntimeInfo& runtimeInfo) const
{
    LOG_DBG() << "Get runtime info";

    runtimeInfo = mRuntimeInfo;

    return ErrorEnum::eNone;
}

Error ContainerRuntime::StartInstance(const InstanceInfo& instanceInfo, InstanceStatus& status)
{
    try {
        std::shared_ptr<Instance> instance;

        {
            std::lock_guard lock {mMutex};

            LOG_DBG() << "Start instance" << Log::Field("instance", static_cast<const InstanceIdent&>(instanceInfo));

            if (auto it = mCurrentInstances.find(static_cast<const InstanceIdent&>(instanceInfo));
                it != mCurrentInstances.end()) {
                return AOS_ERROR_WRAP(Error(ErrorEnum::eAlreadyExist, "instance already running"));
            }

            instance = std::make_shared<Instance>(
                instanceInfo, mConfig, *mFileSystem, *mRunner, *mItemInfoProvider, *mOCISpec);

            mCurrentInstances.insert({static_cast<const InstanceIdent&>(instanceInfo), instance});
        }

        if (auto err = instance->Start(); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        instance->GetStatus(status);

        return ErrorEnum::eNone;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }
}

Error ContainerRuntime::StopInstance(const InstanceIdent& instanceIdent, InstanceStatus& status)
{
    try {
        std::shared_ptr<Instance> instance;

        {
            std::lock_guard lock {mMutex};

            LOG_DBG() << "Stop instance" << Log::Field("instance", instanceIdent);

            auto it = mCurrentInstances.find(static_cast<const InstanceIdent&>(instanceIdent));
            if (it == mCurrentInstances.end()) {
                return AOS_ERROR_WRAP(Error(ErrorEnum::eNotFound, "instance not running"));
            }

            instance = it->second;
            mCurrentInstances.erase(it);
        }

        if (auto err = instance->Stop(); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        instance->GetStatus(status);

        return ErrorEnum::eNone;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }
}

Error ContainerRuntime::Reboot()
{
    LOG_DBG() << "Reboot runtime";

    return ErrorEnum::eNotSupported;
}

Error ContainerRuntime::GetInstanceMonitoringData(
    const InstanceIdent& instanceIdent, monitoring::InstanceMonitoringData& monitoringData)
{
    (void)monitoringData;

    LOG_DBG() << "Get instance monitoring data" << Log::Field("instance", instanceIdent);

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

std::shared_ptr<RunnerItf> ContainerRuntime::CreateRunner()
{
    return std::make_shared<Runner>();
}

std::shared_ptr<FileSystemItf> ContainerRuntime::CreateFileSystem()
{
    return std::make_shared<FileSystem>();
}

Error ContainerRuntime::CreateRuntimeInfo(const std::string& runtimeType, const NodeInfo& nodeInfo)
{
    auto runtimeID = runtimeType + "-" + nodeInfo.mNodeID.CStr();

    if (auto err = mRuntimeInfo.mRuntimeID.Assign(common::utils::NameUUID(runtimeID).c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = mRuntimeInfo.mRuntimeType.Assign(runtimeType.c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    mRuntimeInfo.mOSInfo = nodeInfo.mOSInfo;

    if (nodeInfo.mCPUs.IsEmpty()) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eInvalidArgument, "can't define runtime arch info"));
    }

    mRuntimeInfo.mArchInfo     = nodeInfo.mCPUs[0].mArchInfo;
    mRuntimeInfo.mMaxInstances = cMaxNumInstances;

    LOG_INF() << "Runtime info" << Log::Field("runtimeID", mRuntimeInfo.mRuntimeID)
              << Log::Field("runtimeType", mRuntimeInfo.mRuntimeType)
              << Log::Field("architecture", mRuntimeInfo.mArchInfo.mArchitecture)
              << Log::Field("os", mRuntimeInfo.mOSInfo.mOS) << Log::Field("maxInstances", mRuntimeInfo.mMaxInstances);

    return ErrorEnum::eNone;
}

Error ContainerRuntime::UpdateRunStatus(const std::vector<RunStatus>& instances)
{
    (void)instances;

    return ErrorEnum::eNone;
}

Error ContainerRuntime::StopActiveInstances()
{
    auto [activeInstances, err] = mFileSystem->ListDir(mConfig.mRuntimeDir);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    for (const auto& instanceID : activeInstances) {
        LOG_WRN() << "Try to stop active instance" << Log::Field("instanceID", instanceID.c_str());

        auto instance
            = std::make_unique<Instance>(instanceID, mConfig, *mFileSystem, *mRunner, *mItemInfoProvider, *mOCISpec);

        if (err = instance->Stop(); !err.IsNone()) {
            LOG_ERR() << "Failed to stop active instance" << Log::Field("instanceID", instanceID.c_str())
                      << Log::Field(err);
        }

        LOG_DBG() << "Active instance stopped" << Log::Field("instanceID", instanceID.c_str());
    }

    return ErrorEnum::eNone;
}

}; // namespace aos::sm::launcher
