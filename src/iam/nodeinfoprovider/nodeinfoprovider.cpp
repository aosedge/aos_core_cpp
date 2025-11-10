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

Error SetOSInfo(OSInfo& info)
{
    struct utsname buffer;

    if (auto ret = uname(&buffer); ret != 0) {
        return AOS_ERROR_WRAP(ErrorEnum::eFailed);
    }

    if (auto err = info.mOS.Assign(buffer.sysname); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (buffer.release[0] != '\0') {
        info.mVersion.EmplaceValue();

        if (auto err = info.mVersion->Assign(buffer.release); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    return ErrorEnum::eNone;
}

Error GetNodeState(const std::string& path, NodeState& state, bool& provisioned)
{
    std::ifstream file;

    if (file.open(path); !file.is_open()) {
        state       = NodeStateEnum::eOffline;
        provisioned = false;

        return ErrorEnum::eNone;
    }

    std::string line;
    std::getline(file, line);

    provisioned = true;

    return state.FromString(line.c_str());
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

    if (err = InitOSInfo(config); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    mProvisioningStatusPath = config.mProvisioningStatePath;
    mNodeInfo.mNodeType     = config.mNodeType.c_str();
    mNodeInfo.mTitle        = config.mNodeName.c_str();
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

    err = GetNodeState(mProvisioningStatusPath, mNodeInfo.mState, mNodeInfo.mProvisioned);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error NodeInfoProvider::GetNodeInfo(NodeInfo& nodeInfo) const
{
    std::lock_guard lock {mMutex};

    nodeInfo = mNodeInfo;

    if (auto err = GetNodeState(mProvisioningStatusPath, nodeInfo.mState, nodeInfo.mProvisioned); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error NodeInfoProvider::SetNodeState(const NodeState& state, bool provisioned)
{
    std::lock_guard lock {mMutex};

    if (state == mNodeInfo.mState && provisioned == mNodeInfo.mProvisioned) {
        LOG_DBG() << "Node state is not changed" << Log::Field("state", state)
                  << Log::Field("provisioned", provisioned);

        return ErrorEnum::eNone;
    }

    if (!provisioned) {
        std::filesystem::remove(mProvisioningStatusPath);
    } else {
        std::ofstream file;

        if (file.open(mProvisioningStatusPath, std::ios_base::out | std::ios_base::trunc); !file.is_open()) {
            LOG_ERR() << "Provision status file open failed" << Log::Field("path", mProvisioningStatusPath.c_str());

            return ErrorEnum::eNotFound;
        }

        file << state.ToString().CStr();
    }

    mNodeInfo.mState       = state;
    mNodeInfo.mProvisioned = provisioned;

    LOG_DBG() << "Node state updated" << Log::Field("state", state) << Log::Field("provisioned", provisioned);

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

Error NodeInfoProvider::InitOSInfo(const iam::config::NodeInfoConfig& config)
{
    if (auto err = SetOSInfo(mNodeInfo.mOSInfo); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (!config.mOSType.empty()) {
        return mNodeInfo.mOSInfo.mOS.Assign(config.mOSType.c_str());
    }

    return ErrorEnum::eNone;
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
        if (auto err = mNodeInfo.mPartitions.EmplaceBack(); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        PartitionInfo& partitionInfo = mNodeInfo.mPartitions.Back();

        if (auto err = partitionInfo.mName.Assign(partition.mName.c_str()); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (auto err = partitionInfo.mPath.Assign(partition.mPath.c_str()); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        Error err;

        // cppcheck-suppress unusedScopedObject
        Tie(partitionInfo.mTotalSize, err) = utils::GetMountFSTotalSize(partition.mPath);
        if (!err.IsNone()) {
            LOG_WRN() << "Failed to get total size for partition" << Log::Field("path", partition.mPath.c_str())
                      << Log::Field(err);
        }

        for (const auto& type : partition.mTypes) {
            if (err = partitionInfo.mTypes.EmplaceBack(type.c_str()); !err.IsNone()) {
                return AOS_ERROR_WRAP(err);
            }
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
