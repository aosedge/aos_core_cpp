/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <core/common/tools/logger.hpp>

#include "container.hpp"

namespace aos::sm::launcher {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error ContainerRuntime::Init(const RuntimeConfig& config,
    iamclient::CurrentNodeInfoProviderItf&        currentNodeInfoProvider) // cppcheck-suppress constParameterReference

{
    LOG_DBG() << "Init runtime" << Log::Field("type", config.mType.c_str());

    auto nodeInfo = std::make_unique<NodeInfo>();

    if (auto err = currentNodeInfoProvider.GetCurrentNodeInfo(*nodeInfo); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = CreateRuntimeInfo(config.mType, *nodeInfo); !err.IsNone()) {
        return err;
    }

    return ErrorEnum::eNone;
}

Error ContainerRuntime::Start()
{
    LOG_DBG() << "Start runtime";

    return ErrorEnum::eNone;
}

Error ContainerRuntime::Stop()
{
    LOG_DBG() << "Stop runtime";

    return ErrorEnum::eNone;
}

Error ContainerRuntime::GetRuntimeInfo(RuntimeInfo& runtimeInfo) const
{
    LOG_DBG() << "Get runtime info";

    runtimeInfo = mRuntimeInfo;

    return ErrorEnum::eNone;
}

Error ContainerRuntime::StartInstance(const InstanceInfo& instance, InstanceStatus& status)
{
    (void)status;

    LOG_DBG() << "Start instance" << Log::Field("instance", static_cast<const InstanceIdent&>(instance));

    return ErrorEnum::eNone;
}

Error ContainerRuntime::StopInstance(const InstanceIdent& instance, InstanceStatus& status)
{
    (void)status;

    LOG_DBG() << "Stop instance" << Log::Field("instance", instance);

    return ErrorEnum::eNone;
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

Error ContainerRuntime::CreateRuntimeInfo(const std::string& runtimeType, const NodeInfo& nodeInfo)
{
    auto runtimeID = runtimeType + "-" + nodeInfo.mNodeID.CStr();

    if (auto err = mRuntimeInfo.mRuntimeID.Assign(runtimeID.c_str()); !err.IsNone()) {
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

}; // namespace aos::sm::launcher
