/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>

#include <core/common/tools/logger.hpp>
#include <core/common/tools/uuid.hpp>

#include <common/utils/exception.hpp>

#include "currentnodehandler.hpp"
#include "systeminfo.hpp"

namespace aos::iam::currentnode {

namespace {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

Error ReadIDFromFile(const std::string& path, String& id)
{
    std::ifstream file;

    if (file.open(path); !file.is_open()) {
        return ErrorEnum::eNotFound;
    }

    std::string line;

    if (!std::getline(file, line)) {
        return ErrorEnum::eFailed;
    }

    if (auto err = id.Assign(line.c_str()); !err.IsNone()) {
        return err;
    }

    id.Trim(" \t\r\n");

    if (id.IsEmpty()) {
        return Error(ErrorEnum::eInvalidArgument, "hardware ID is empty");
    }

    return ErrorEnum::eNone;
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error CurrentNodeHandler::Init(const iam::config::NodeInfoConfig& config, crypto::UUIDItf& uuidProvider)
{
    LOG_DBG() << "Init current node handler";

    if (auto err = InitNodeID(config.mHardwareIDPath, uuidProvider); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = InitOSInfo(config); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    mProvisioningStatusPath = config.mProvisioningStatePath;
    mNodeInfo.mNodeType     = config.mNodeType.c_str();
    mNodeInfo.mTitle        = config.mNodeName.c_str();
    mNodeInfo.mMaxDMIPS     = config.mMaxDMIPS;

    Error err;

    // cppcheck-suppress unusedScopedObject
    Tie(mNodeInfo.mTotalRAM, err) = utils::GetMemTotal(config.mMemInfoPath);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto initErr = InitAtrributesInfo(config); !initErr.IsNone()) {
        return AOS_ERROR_WRAP(initErr);
    }

    if (auto initErr = InitCPUInfo(config); !initErr.IsNone()) {
        return AOS_ERROR_WRAP(initErr);
    }

    if (auto initErr = InitPartitionInfo(config); !initErr.IsNone()) {
        return AOS_ERROR_WRAP(initErr);
    }

    if (auto stateErr = ReadNodeState(); !stateErr.IsNone()) {
        LOG_ERR() << "Failed to read node state" << Log::Field(stateErr);

        mNodeInfo.mState = NodeStateEnum::eError;
        mNodeInfo.mError = stateErr;
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

Error CurrentNodeHandler::InitNodeID(const std::string& hardwareIDPath, crypto::UUIDItf& uuidProvider)
{
    StaticString<cHardwareIDLen> hardwareID;

    if (auto err = ReadIDFromFile(hardwareIDPath, hardwareID); !err.IsNone()) {
        return err;
    }

    uuid::UUID space;
    Error      err;

    Tie(space, err) = uuid::StringToUUID(cNodeIDNamespaceUUID);
    if (!err.IsNone()) {
        return err;
    }

    uuid::UUID nodeUUID;

    Tie(nodeUUID, err) = uuidProvider.CreateUUIDv5(space, hardwareID.AsByteArray());
    if (!err.IsNone()) {
        return err;
    }

    if (auto assignErr = mNodeInfo.mNodeID.Assign(uuid::UUIDToString(nodeUUID).CStr()); !assignErr.IsNone()) {
        return assignErr;
    }

    LOG_INF() << "Node identity initialized" << Log::Field("hardwareID", hardwareID)
              << Log::Field("nodeID", mNodeInfo.mNodeID);

    return ErrorEnum::eNone;
}

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

Error CurrentNodeHandler::InitCPUInfo(const iam::config::NodeInfoConfig& config)
{
    if (auto err = utils::GetCPUInfo(config.mCPUInfoPath, mNodeInfo.mCPUs); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    for (auto& cpu : mNodeInfo.mCPUs) {
        if (config.mArchitecture.has_value()) {
            if (auto err = cpu.mArchInfo.mArchitecture.Assign(config.mArchitecture->c_str()); !err.IsNone()) {
                return AOS_ERROR_WRAP(err);
            }
        }

        if (config.mArchitectureVariant.has_value()) {
            cpu.mArchInfo.mVariant.EmplaceValue();

            if (auto err = cpu.mArchInfo.mVariant->Assign(config.mArchitectureVariant->c_str()); !err.IsNone()) {
                return AOS_ERROR_WRAP(err);
            }
        }
    }

    return ErrorEnum::eNone;
}

Error CurrentNodeHandler::InitOSInfo(const iam::config::NodeInfoConfig& config)
{
    if (config.mOS.has_value()) {
        if (auto err = mNodeInfo.mOSInfo.mOS.Assign(config.mOS->c_str()); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    if (config.mOSVersion.has_value()) {
        mNodeInfo.mOSInfo.mVersion.EmplaceValue();

        if (auto err = mNodeInfo.mOSInfo.mVersion->Assign(config.mOSVersion->c_str()); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
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
            if (auto typeErr = partitionInfo.mTypes.EmplaceBack(type.c_str()); !typeErr.IsNone()) {
                return AOS_ERROR_WRAP(typeErr);
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
