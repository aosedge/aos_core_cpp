/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>
#include <sys/utsname.h>

#include <common/logger/logmodule.hpp>
#include <common/utils/exception.hpp>

#include "nodeinfoprovider.hpp"
#include "systeminfo.hpp"

namespace aos::iam::nodeinfoprovider {

namespace {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

Error GetOSType(String& osType)
{
    struct utsname buffer;

    if (auto ret = uname(&buffer); ret != 0) {
        return AOS_ERROR_WRAP(ErrorEnum::eFailed);
    }

    return osType.Assign(buffer.sysname);
}

RetWithError<NodeStateObsolete> GetNodeState(const std::string& path)
{
    std::ifstream file;

    if (file.open(path); !file.is_open()) {
        // .provisionstate file doesn't exist => state unprovisioned
        return {NodeStateObsoleteEnum::eUnprovisioned, ErrorEnum::eNone};
    }

    std::string line;
    std::getline(file, line);

    NodeStateObsolete nodeState;

    auto err = nodeState.FromString(line.c_str());

    return {nodeState, err};
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

    if (err = InitOSType(config); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    mProvisioningStatusPath = config.mProvisioningStatePath;
    mNodeInfo.mNodeType     = config.mNodeType.c_str();
    mNodeInfo.mName         = config.mNodeName.c_str();
    mNodeInfo.mMaxDMIPS     = config.mMaxDMIPS;

    // cppcheck-suppress unusedScopedObject
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

    // cppcheck-suppress unusedScopedObject
    Tie(mNodeInfo.mState, err) = GetNodeState(mProvisioningStatusPath);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error NodeInfoProvider::GetNodeInfo(NodeInfoObsolete& nodeInfo) const
{
    std::lock_guard lock {mMutex};

    Error             err;
    NodeStateObsolete state;

    // cppcheck-suppress unusedScopedObject
    Tie(state, err) = GetNodeState(mProvisioningStatusPath);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    nodeInfo        = mNodeInfo;
    nodeInfo.mState = state;

    return ErrorEnum::eNone;
}

Error NodeInfoProvider::SetNodeState(const NodeStateObsolete& state)
{
    std::lock_guard lock {mMutex};

    if (state == mNodeInfo.mState) {
        LOG_DBG() << "Node state is not changed: state=" << state.ToString();

        return ErrorEnum::eNone;
    }

    if (state == NodeStateObsoleteEnum::eUnprovisioned) {
        std::filesystem::remove(mProvisioningStatusPath);
    } else {
        std::ofstream file;

        if (file.open(mProvisioningStatusPath, std::ios_base::out | std::ios_base::trunc); !file.is_open()) {
            LOG_ERR() << "Provision status file open failed: path=" << mProvisioningStatusPath.c_str();

            return ErrorEnum::eNotFound;
        }

        file << state.ToString().CStr();
    }

    mNodeInfo.mState = state;

    LOG_DBG() << "Node state updated: state=" << state.ToString();

    if (auto err = NotifyNodeStateChanged(); !err.IsNone()) {
        return AOS_ERROR_WRAP(Error(err, "failed to notify node state changed subscribers"));
    }

    return ErrorEnum::eNone;
}

Error NodeInfoProvider::SubscribeNodeStateChanged(iam::nodeinfoprovider::NodeStateObserverItf& observer)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Subscribe node state changed observer";

    try {
        mObservers.insert(&observer);
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error NodeInfoProvider::UnsubscribeNodeStateChanged(iam::nodeinfoprovider::NodeStateObserverItf& observer)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Unsubscribe node state changed observer";

    mObservers.erase(&observer);

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

Error NodeInfoProvider::InitOSType(const iam::config::NodeInfoConfig& config)
{
    if (!config.mOSType.empty()) {
        return mNodeInfo.mOSType.Assign(config.mOSType.c_str());
    }

    return GetOSType(mNodeInfo.mOSType);
}

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
        PartitionInfoObsolete partitionInfo = {};

        partitionInfo.mName = partition.mName.c_str();
        partitionInfo.mPath = partition.mPath.c_str();

        Error err;

        // cppcheck-suppress unusedScopedObject
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

Error NodeInfoProvider::NotifyNodeStateChanged()
{
    Error err;

    for (auto observer : mObservers) {
        LOG_DBG() << "Notify node state changed observer: nodeID=" << mNodeInfo.mNodeID.CStr()
                  << ", state=" << mNodeInfo.mState.ToString();

        auto errNotify = observer->OnNodeStateChanged(mNodeInfo.mNodeID, mNodeInfo.mState);
        if (err.IsNone() && !errNotify.IsNone()) {
            err = errNotify;
        }
    }

    return err;
}

} // namespace aos::iam::nodeinfoprovider
