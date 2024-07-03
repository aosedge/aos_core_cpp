/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>

#include <utils/exception.hpp>

#include "logger/logmodule.hpp"
#include "nodeinfoprovider.hpp"
#include "systeminfo.hpp"

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

static aos::RetWithError<aos::NodeStatus> GetNodeStatus(const std::string& path)
{
    std::ifstream file;

    if (file.open(path); !file.is_open()) {
        return {aos::NodeStatusEnum::eUnprovisioned, aos::ErrorEnum::eNotFound};
    }

    std::string line;
    std::getline(file, line);

    aos::NodeStatus nodeStatus;
    auto            err = nodeStatus.FromString(line.c_str());

    return {nodeStatus, err};
}

static aos::Error GetNodeID(const std::string& path, aos::String& nodeID)
{
    std::ifstream file;

    if (file.open(path); !file.is_open()) {
        return aos::ErrorEnum::eNotFound;
    }

    std::string line;

    if (!std::getline(file, line)) {
        return aos::ErrorEnum::eFailed;
    }

    nodeID = line.c_str();

    return aos::ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

aos::Error NodeInfoProvider::Init(const NodeInfoConfig& config)
{
    aos::Error err;

    if (err = GetNodeID(config.mNodeIDPath, mNodeInfo.mNodeID); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    mProvisioningStatusPath = config.mProvisioningStatePath;
    mNodeInfo.mNodeType     = config.mNodeType.c_str();
    mNodeInfo.mName         = config.mNodeName.c_str();
    mNodeInfo.mOSType       = config.mOSType.c_str();
    mNodeInfo.mMaxDMIPS     = config.mMaxDMIPS;

    aos::Tie(mNodeInfo.mTotalRAM, err) = UtilsSystemInfo::GetMemTotal(config.mMemInfoPath);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (err = InitAtrributesInfo(config); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (err = UtilsSystemInfo::GetCPUInfo(config.mCPUInfoPath, mNodeInfo.mCPUs); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (err = InitPartitionInfo(config); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return aos::ErrorEnum::eNone;
}

aos::Error NodeInfoProvider::GetNodeInfo(aos::NodeInfo& nodeInfo) const
{
    aos::Error      err;
    aos::NodeStatus status;

    aos::Tie(status, err) = GetNodeStatus(mProvisioningStatusPath);
    if (!err.IsNone() && !err.Is(aos::ErrorEnum::eNotFound)) {
        return AOS_ERROR_WRAP(err);
    }

    nodeInfo         = mNodeInfo;
    nodeInfo.mStatus = status;

    return aos::ErrorEnum::eNone;
}

aos::Error NodeInfoProvider::SetNodeStatus(const aos::NodeStatus& status)
{
    std::ofstream file;

    if (file.open(mProvisioningStatusPath, std::ios_base::out | std::ios_base::trunc); !file.is_open()) {
        LOG_ERR() << "Provision status file open failed: path=" << mProvisioningStatusPath.c_str();

        return aos::ErrorEnum::eNotFound;
    }

    file << status.ToString().CStr();

    LOG_DBG() << "Node status updated: status=" << status.ToString();

    return aos::ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

aos::Error NodeInfoProvider::InitAtrributesInfo(const NodeInfoConfig& config)
{
    for (const auto& [name, value] : config.mAttrs) {
        if (auto err = mNodeInfo.mAttrs.PushBack(aos::NodeAttribute {name.c_str(), value.c_str()}); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    return aos::ErrorEnum::eNone;
}

aos::Error NodeInfoProvider::InitPartitionInfo(const NodeInfoConfig& config)
{
    for (const auto& partition : config.mPartitions) {
        aos::PartitionInfo partitionInfo;

        partitionInfo.mName = partition.mName.c_str();
        partitionInfo.mPath = partition.mPath.c_str();

        aos::Error err;

        aos::Tie(partitionInfo.mTotalSize, err) = UtilsSystemInfo::GetMountFSTotalSize(partition.mPath);
        if (!err.IsNone()) {
            LOG_WRN() << "Failed to get total size for partition: path=" << partition.mPath.c_str() << ", err=" << err;
        }

        for (const auto& type : partition.mTypes) {
            if (err = partitionInfo.mTypes.PushBack(type.c_str()); !err.IsNone()) {
                return AOS_ERROR_WRAP(err);
            }
        }

        if (err = mNodeInfo.mPartitions.PushBack(partitionInfo); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    return aos::ErrorEnum::eNone;
}
