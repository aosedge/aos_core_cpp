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

namespace aos::iam::nodeinfoprovider {

namespace {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

RetWithError<NodeStatus> GetNodeStatus(const std::string& path)
{
    std::ifstream file;

    if (file.open(path); !file.is_open()) {
        // .provisionstate file doesn't exist => state unprovisioned
        return {NodeStatusEnum::eUnprovisioned, ErrorEnum::eNone};
    }

    std::string line;
    std::getline(file, line);

    NodeStatus nodeStatus;
    auto       err = nodeStatus.FromString(line.c_str());

    return {nodeStatus, err};
}

Error GetNodeID(const std::string& path, String& nodeID)
{
    std::ifstream file;

    if (file.open(path); !file.is_open()) {
        return ErrorEnum::eNotFound;
    }

    std::string line;

    if (!std::getline(file, line)) {
        return ErrorEnum::eFailed;
    }

    nodeID = line.c_str();

    return ErrorEnum::eNone;
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error NodeInfoProvider::Init(const iam::config::NodeInfoConfig& config)
{
    Error err;

    if (err = GetNodeID(config.mNodeIDPath, mNodeInfo.mNodeID); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    mProvisioningStatusPath = config.mProvisioningStatePath;
    mNodeInfo.mNodeType     = config.mNodeType.c_str();
    mNodeInfo.mName         = config.mNodeName.c_str();
    mNodeInfo.mOSType       = config.mOSType.c_str();
    mNodeInfo.mMaxDMIPS     = config.mMaxDMIPS;

    Tie(mNodeInfo.mTotalRAM, err) = utils::GetMemTotal(config.mMemInfoPath);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (err = InitAtrributesInfo(config); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (err = utils::GetCPUInfo(config.mCPUInfoPath, mNodeInfo.mCPUs); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (err = InitPartitionInfo(config); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    Tie(mNodeInfo.mStatus, err) = GetNodeStatus(mProvisioningStatusPath);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error NodeInfoProvider::GetNodeInfo(NodeInfo& nodeInfo) const
{
    std::lock_guard lock {mMutex};

    Error      err;
    NodeStatus status;

    Tie(status, err) = GetNodeStatus(mProvisioningStatusPath);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    nodeInfo         = mNodeInfo;
    nodeInfo.mStatus = status;

    return ErrorEnum::eNone;
}

Error NodeInfoProvider::SetNodeStatus(const NodeStatus& status)
{
    std::lock_guard lock {mMutex};

    if (status == mNodeInfo.mStatus) {
        LOG_DBG() << "Node status is not changed: status=" << status.ToString();

        return ErrorEnum::eNone;
    }

    if (status == NodeStatusEnum::eUnprovisioned) {
        std::filesystem::remove(mProvisioningStatusPath);
    } else {
        std::ofstream file;

        if (file.open(mProvisioningStatusPath, std::ios_base::out | std::ios_base::trunc); !file.is_open()) {
            LOG_ERR() << "Provision status file open failed: path=" << mProvisioningStatusPath.c_str();

            return ErrorEnum::eNotFound;
        }

        file << status.ToString().CStr();
    }

    mNodeInfo.mStatus = status;

    LOG_DBG() << "Node status updated: status=" << status.ToString();

    if (auto err = NotifyNodeStatusChanged(); !err.IsNone()) {
        return AOS_ERROR_WRAP(Error(err, "failed to notify node status changed subscribers"));
    }

    return ErrorEnum::eNone;
}

Error NodeInfoProvider::SubscribeNodeStatusChanged(iam::nodeinfoprovider::NodeStatusObserverItf& observer)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Subscribe node status changed observer";

    try {
        mObservers.insert(&observer);
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error NodeInfoProvider::UnsubscribeNodeStatusChanged(iam::nodeinfoprovider::NodeStatusObserverItf& observer)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Unsubscribe node status changed observer";

    mObservers.erase(&observer);

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

Error NodeInfoProvider::InitAtrributesInfo(const iam::config::NodeInfoConfig& config)
{
    for (const auto& [name, value] : config.mAttrs) {
        if (auto err = mNodeInfo.mAttrs.PushBack(NodeAttribute {name.c_str(), value.c_str()}); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    return ErrorEnum::eNone;
}

Error NodeInfoProvider::InitPartitionInfo(const iam::config::NodeInfoConfig& config)
{
    for (const auto& partition : config.mPartitions) {
        PartitionInfo partitionInfo;

        partitionInfo.mName = partition.mName.c_str();
        partitionInfo.mPath = partition.mPath.c_str();

        Error err;

        Tie(partitionInfo.mTotalSize, err) = utils::GetMountFSTotalSize(partition.mPath);
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

    return ErrorEnum::eNone;
}

Error NodeInfoProvider::NotifyNodeStatusChanged()
{
    Error err;

    for (auto observer : mObservers) {
        LOG_DBG() << "Notify node status changed observer: nodeID=" << mNodeInfo.mNodeID.CStr()
                  << ", status=" << mNodeInfo.mStatus.ToString();

        auto errNotify = observer->OnNodeStatusChanged(mNodeInfo.mNodeID, mNodeInfo.mStatus);
        if (err.IsNone() && !errNotify.IsNone()) {
            err = errNotify;
        }
    }

    return err;
}

} // namespace aos::iam::nodeinfoprovider
