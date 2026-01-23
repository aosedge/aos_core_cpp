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
#include "monitoring.hpp"
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
    iamclient::CurrentNodeInfoProviderItf&        currentNodeInfoProvider, // cppcheck-suppress constParameterReference
    imagemanager::ItemInfoProviderItf& itemInfoProvider, networkmanager::NetworkManagerItf& networkManager,
    iamclient::PermHandlerItf& permHandler, resourcemanager::ResourceInfoProviderItf& resourceInfoProvider,
    oci::OCISpecItf& ociSpec, InstanceStatusReceiverItf& instanceStatusReceiver)
{
    try {
        LOG_DBG() << "Init runtime" << Log::Field("type", config.mType.c_str());

        if (auto err = currentNodeInfoProvider.GetCurrentNodeInfo(mNodeInfo); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (auto err = CreateRuntimeInfo(config.mType, mNodeInfo); !err.IsNone()) {
            return err;
        }

        mRunner     = CreateRunner();
        mFileSystem = CreateFileSystem();
        mMonitoring = CreateMonitoring();

        mItemInfoProvider       = &itemInfoProvider;
        mNetworkManager         = &networkManager;
        mPermHandler            = &permHandler;
        mResourceInfoProvider   = &resourceInfoProvider;
        mOCISpec                = &ociSpec;
        mInstanceStatusReceiver = &instanceStatusReceiver;

        if (auto err = mRunner->Init(*this); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (auto err = mMonitoring->Init(*mNetworkManager); !err.IsNone()) {
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

        if (auto err = mFileSystem->MakeDirAll(mConfig.mRuntimeDir); !err.IsNone()) {
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

                instance = it->second;
                instance->GetStatus(status);

                if (status.mState == InstanceStateEnum::eActive) {
                    LOG_DBG() << "Instance is already running"
                              << Log::Field("instance", static_cast<const InstanceIdent&>(instanceInfo));

                    return ErrorEnum::eNone;
                }
            }
        }

        if (instance) {
            if (auto err = instance->Stop(); !err.IsNone()) {
                LOG_ERR() << "Failed to stop instance"
                          << Log::Field("instance", static_cast<const InstanceIdent&>(instanceInfo)) << Log::Field(err);
            }

            instance->GetStatus(status);
            SendInstanceStatus(status);
        } else {
            std::lock_guard lock {mMutex};

            instance = std::make_shared<Instance>(instanceInfo, mConfig, mNodeInfo, *mFileSystem, *mRunner,
                *mMonitoring, *mItemInfoProvider, *mNetworkManager, *mPermHandler, *mResourceInfoProvider, *mOCISpec);

            mCurrentInstances.insert({static_cast<const InstanceIdent&>(instanceInfo), instance});
        }

        instance->UpdateRunStatus(RunStatus {instance->InstanceID(), InstanceStateEnum::eActivating, ErrorEnum::eNone});
        instance->GetStatus(status);
        SendInstanceStatus(status);

        auto sendStatus = DeferRelease(instance.get(), [&](void*) {
            instance->GetStatus(status);
            SendInstanceStatus(status);
        });

        if (auto err = instance->Start(); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

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
                LOG_DBG() << "Instance is not running" << Log::Field("instance", instanceIdent);

                return ErrorEnum::eNone;
            }

            instance = it->second;
            mCurrentInstances.erase(it);
        }

        auto sendStatus = DeferRelease(instance.get(), [&](void*) {
            instance->GetStatus(status);
            SendInstanceStatus(status);
        });

        if (auto err = instance->Stop(); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

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
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Get instance monitoring data" << Log::Field("instance", instanceIdent);

    auto it = mCurrentInstances.find(static_cast<const InstanceIdent&>(instanceIdent));
    if (it == mCurrentInstances.end()) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eNotFound, "instance not found"));
    }

    if (auto err = mMonitoring->GetInstanceMonitoringData(it->second->InstanceID(), monitoringData); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    monitoringData.mInstanceIdent = instanceIdent;
    monitoringData.mRuntimeID     = mRuntimeInfo.mRuntimeID;

    return ErrorEnum::eNone;
}

Error ContainerRuntime::GetInstanceInfoByID(const String& instanceID, alerts::InstanceInfo& instanceInfo)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Get instance info by ID" << Log::Field("instanceID", instanceID.CStr());

    auto it = std::find_if(mCurrentInstances.begin(), mCurrentInstances.end(),
        [&instanceID](const auto& pair) { return pair.second->InstanceID() == instanceID.CStr(); });
    if (it == mCurrentInstances.end()) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eNotFound, "instance not found"));
    }

    instanceInfo.mInstanceIdent = it->first;

    if (auto err = instanceInfo.mVersion.Assign(it->second->GetVersion().c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error ContainerRuntime::GetInstanceIDs(const LogFilter& filter, std::vector<std::string>& instanceIDs)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Get instance IDs" << Log::Field("filter", filter);

    std::for_each(mCurrentInstances.begin(), mCurrentInstances.end(), [&instanceIDs, &filter](const auto& pair) {
        if (filter.Match(static_cast<const InstanceIdent&>(pair.first))) {
            instanceIDs.push_back(pair.second->InstanceID());
        }
    });

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

std::shared_ptr<MonitoringItf> ContainerRuntime::CreateMonitoring()
{
    return std::make_shared<Monitoring>();
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
    std::lock_guard lock {mMutex};

    std::vector<InstanceStatus> instancesStatuses;

    for (const auto& runStatus : instances) {
        auto it = std::find_if(mCurrentInstances.begin(), mCurrentInstances.end(),
            [&runStatus](const auto& pair) { return pair.second->InstanceID() == runStatus.mInstanceID; });
        if (it == mCurrentInstances.end()) {
            LOG_WRN() << "Received run status for unknown instance"
                      << Log::Field("instanceID", runStatus.mInstanceID.c_str());

            continue;
        }

        LOG_DBG() << "Update run status" << Log::Field("instanceID", runStatus.mInstanceID.c_str())
                  << Log::Field("state", runStatus.mState) << Log::Field(runStatus.mError);

        if (it->second->UpdateRunStatus(runStatus)) {
            instancesStatuses.emplace_back();
            it->second->GetStatus(instancesStatuses.back());
        }
    }

    if (!instancesStatuses.empty()) {
        mInstanceStatusReceiver->OnInstancesStatusesReceived(
            Array<InstanceStatus>(instancesStatuses.data(), instancesStatuses.size()));
    }

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

        auto instance = std::make_unique<Instance>(instanceID, mConfig, mNodeInfo, *mFileSystem, *mRunner, *mMonitoring,
            *mItemInfoProvider, *mNetworkManager, *mPermHandler, *mResourceInfoProvider, *mOCISpec);

        if (err = instance->Stop(); !err.IsNone()) {
            LOG_ERR() << "Failed to stop active instance" << Log::Field("instanceID", instanceID.c_str())
                      << Log::Field(err);
        }

        LOG_DBG() << "Active instance stopped" << Log::Field("instanceID", instanceID.c_str());
    }

    return ErrorEnum::eNone;
}

void ContainerRuntime::SendInstanceStatus(const InstanceStatus& status)
{
    mInstanceStatusReceiver->OnInstancesStatusesReceived(Array<InstanceStatus>(&status, 1));
}

} // namespace aos::sm::launcher
