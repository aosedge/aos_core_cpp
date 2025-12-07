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

#include "currentnodehandler.hpp"
#include "systeminfo.hpp"

namespace aos::iam::currentnode {

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

Error CurrentNodeHandler::Init(const iam::config::NodeInfoConfig& config)
{
    Error err;

    LOG_INF() << "Initialize current node handler";

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

    if (err = ReadNodeState(); !err.IsNone()) {
        LOG_ERR() << "Failed to read node state" << Log::Field(err);

        mNodeInfo.mState = NodeStateEnum::eError;
        mNodeInfo.mError = err;
    }

    return ErrorEnum::eNone;
}

Error CurrentNodeHandler::GetCurrentNodeInfo(NodeInfo& nodeInfo) const
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Get current node info" << Log::Field("nodeID", mNodeInfo.mNodeID)
              << Log::Field("state", mNodeInfo.mState) << Log::Field("isConnected", mNodeInfo.mIsConnected);

    nodeInfo = mNodeInfo;

    return ErrorEnum::eNone;
}

Error CurrentNodeHandler::SubscribeListener(iamclient::CurrentNodeInfoListenerItf& listener)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Subscribe current node info changed listener";

    try {
        mListeners.insert(&listener);
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error CurrentNodeHandler::UnsubscribeListener(iamclient::CurrentNodeInfoListenerItf& listener)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Unsubscribe current node info changed listener";

    mListeners.erase(&listener);

    return ErrorEnum::eNone;
}

Error CurrentNodeHandler::SetState(NodeState state)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Set current node state" << Log::Field("nodeID", mNodeInfo.mNodeID) << Log::Field("state", state);

    if (mNodeInfo.mState == state) {
        LOG_DBG() << "Node is already in the requested state" << Log::Field("state", state);

        return ErrorEnum::eNone;
    }

    if (auto err = UpdateProvisionFile(state); !err.IsNone()) {
        return err;
    }

    mNodeInfo.mState = state;

    NotifyNodeInfoChanged();

    return ErrorEnum::eNone;
}

Error CurrentNodeHandler::SetConnected(bool isConnected)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Set current node connected" << Log::Field("nodeID", mNodeInfo.mNodeID)
              << Log::Field("connected", isConnected);

    if (mNodeInfo.mIsConnected == isConnected) {
        LOG_DBG() << "Node is already in the requested connected state" << Log::Field("isConnected", isConnected);

        return ErrorEnum::eNone;
    }

    mNodeInfo.mIsConnected = isConnected;

    NotifyNodeInfoChanged();

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

Error CurrentNodeHandler::ReadNodeState()
{
    std::ifstream file;

    if (file.open(mProvisioningStatusPath); !file.is_open()) {
        mNodeInfo.mState = NodeStateEnum::eUnprovisioned;

        return ErrorEnum::eNone;
    }

    std::string line;

    std::getline(file, line);

    if (auto err = mNodeInfo.mState.FromString(line.c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error CurrentNodeHandler::UpdateProvisionFile(NodeState state)
{
    if (state == NodeStateEnum::eUnprovisioned) {
        std::filesystem::remove(mProvisioningStatusPath);

        return ErrorEnum::eNone;
    }

    std::ofstream file;

    if (file.open(mProvisioningStatusPath, std::ios_base::out | std::ios_base::trunc); !file.is_open()) {
        LOG_ERR() << "Provision status file open failed" << Log::Field("path", mProvisioningStatusPath.c_str());

        return ErrorEnum::eNotFound;
    }

    file << state.ToString().CStr();
    file.close();

    return ErrorEnum::eNone;
}

Error CurrentNodeHandler::InitOSInfo(const iam::config::NodeInfoConfig& config)
{
    if (auto err = SetOSInfo(mNodeInfo.mOSInfo); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (!config.mOSType.empty()) {
        return mNodeInfo.mOSInfo.mOS.Assign(config.mOSType.c_str());
    }

    return ErrorEnum::eNone;
}

Error CurrentNodeHandler::InitAtrributesInfo(const iam::config::NodeInfoConfig& config)
{
    for (const auto& [name, value] : config.mAttrs) {
        if (auto err = mNodeInfo.mAttrs.PushBack(NodeAttribute {name.c_str(), value.c_str()}); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    return ErrorEnum::eNone;
}

Error CurrentNodeHandler::InitPartitionInfo(const iam::config::NodeInfoConfig& config)
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

void CurrentNodeHandler::NotifyNodeInfoChanged()
{
    for (auto listener : mListeners) {
        LOG_DBG() << "Notify node info changed listeners" << Log::Field("nodeID", mNodeInfo.mNodeID)
                  << Log::Field("state", mNodeInfo.mState);

        listener->OnCurrentNodeInfoChanged(mNodeInfo);
    }
}

} // namespace aos::iam::currentnode
