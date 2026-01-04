/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <core/common/tools/logger.hpp>

#include <common/utils/utils.hpp>

#include "boot.hpp"

namespace aos::sm::launcher {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

// cppcheck-suppress constParameterReference
Error BootRuntime::Init(const RuntimeConfig& config, iamclient::CurrentNodeInfoProviderItf& currentNodeInfoProvider)
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

Error BootRuntime::Start()
{
    LOG_DBG() << "Start runtime";

    return ErrorEnum::eNone;
}

Error BootRuntime::Stop()
{
    LOG_DBG() << "Stop runtime";

    return ErrorEnum::eNone;
}

Error BootRuntime::GetRuntimeInfo(RuntimeInfo& runtimeInfo) const
{
    LOG_DBG() << "Get runtime info";

    runtimeInfo = mRuntimeInfo;

    return ErrorEnum::eNone;
}

Error BootRuntime::StartInstance(const InstanceInfo& instance, InstanceStatus& status)
{
    (void)status;

    LOG_DBG() << "Start instance" << Log::Field("instance", static_cast<const InstanceIdent&>(instance));

    return ErrorEnum::eNone;
}

Error BootRuntime::StopInstance(const InstanceIdent& instance, InstanceStatus& status)
{
    (void)status;

    LOG_DBG() << "Stop instance" << Log::Field("instance", instance);

    return ErrorEnum::eNone;
}

Error BootRuntime::Reboot()
{
    LOG_DBG() << "Reboot runtime";

    return ErrorEnum::eNotSupported;
}

Error BootRuntime::GetInstanceMonitoringData(
    const InstanceIdent& instanceIdent, monitoring::InstanceMonitoringData& monitoringData)
{
    (void)monitoringData;

    LOG_DBG() << "Get instance monitoring data" << Log::Field("instance", instanceIdent);

    return ErrorEnum::eNotSupported;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

Error BootRuntime::CreateRuntimeInfo(const std::string& runtimeType, const NodeInfo& nodeInfo)
{
    auto runtimeID = runtimeType + "-" + nodeInfo.mNodeID.CStr();

    if (auto err = mRuntimeInfo.mRuntimeID.Assign(common::utils::NameUUID(runtimeID).c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = mRuntimeInfo.mRuntimeType.Assign(runtimeType.c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    mRuntimeInfo.mMaxInstances = 1;

    LOG_INF() << "Runtime info" << Log::Field("runtimeID", mRuntimeInfo.mRuntimeID)
              << Log::Field("runtimeType", mRuntimeInfo.mRuntimeType)
              << Log::Field("maxInstances", mRuntimeInfo.mMaxInstances);

    return ErrorEnum::eNone;
}

}; // namespace aos::sm::launcher
