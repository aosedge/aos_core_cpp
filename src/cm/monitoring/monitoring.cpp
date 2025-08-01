/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <common/logger/logmodule.hpp>

#include "monitoring.hpp"

namespace aos::cm::monitoring {

namespace {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

Error CreateMonitoringData(const aos::monitoring::MonitoringData& monitoringData, const Time& timestamp,
    cloudprotocol::MonitoringData& cloudMonitoring)
{
    cloudMonitoring.mTime     = timestamp;
    cloudMonitoring.mCPU      = static_cast<size_t>(monitoringData.mCPU);
    cloudMonitoring.mRAM      = monitoringData.mRAM;
    cloudMonitoring.mDownload = monitoringData.mDownload;
    cloudMonitoring.mUpload   = monitoringData.mUpload;

    for (const auto& partition : monitoringData.mPartitions) {
        if (auto err = cloudMonitoring.mPartitions.EmplaceBack(); !err.IsNone()) {
            return AOS_ERROR_WRAP(Error(err, "can't add partition to monitoring data"));
        }

        if (auto err = cloudMonitoring.mPartitions.Back().mName.Assign(partition.mName); !err.IsNone()) {
            return AOS_ERROR_WRAP(Error(err, "can't assign partition name"));
        }

        cloudMonitoring.mPartitions.Back().mUsedSize = partition.mUsedSize;
    }

    return ErrorEnum::eNone;
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error Monitoring::Init(const config::Monitoring& config, communication::CommunicationItf& communication)
{
    LOG_DBG() << "Initialize monitoring";

    mConfig        = config;
    mCommunication = &communication;

    return ErrorEnum::eNone;
}

Error Monitoring::Start()
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Start monitoring module";

    if (mIsRunning) {
        return ErrorEnum::eWrongState;
    }

    Poco::TimerCallback<Monitoring> callback(*this, &Monitoring::ProcessMonitoring);

    mSendMonitoringTimer.setStartInterval(mConfig.mSendPeriod.Milliseconds());
    mSendMonitoringTimer.setPeriodicInterval(mConfig.mSendPeriod.Milliseconds());
    mSendMonitoringTimer.start(callback);

    mIsRunning = true;

    return ErrorEnum::eNone;
}

Error Monitoring::Stop()
{
    {
        std::lock_guard lock {mMutex};

        LOG_DBG() << "Stop monitoring module";

        if (!mIsRunning) {
            return ErrorEnum::eWrongState;
        }

        mSendMonitoringTimer.stop();
        mIsRunning = false;
    }

    return ErrorEnum::eNone;
}

Error Monitoring::SendMonitoringData(const aos::monitoring::NodeMonitoringData& monitoringData)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Send monitoring data" << Log::Field("nodeID", monitoringData.mNodeID);

    return CacheMonitoringData(monitoringData);
}

void Monitoring::OnConnect()
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Publisher connected";

    mIsConnected = true;
}

void Monitoring::OnDisconnect()
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Publisher disconnected";

    mIsConnected = false;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

bool Monitoring::CanAddNodesToLastPackage(const String& nodeID) const
{
    const auto& last    = mMonitoring.back();
    const auto  itNodes = last.mNodes.FindIf([&nodeID](const auto& node) { return node.mNodeID == nodeID; });

    if (itNodes == last.mNodes.end()) {
        return !last.mNodes.IsFull();
    }

    return !itNodes->mItems.IsFull();
}

bool Monitoring::CanAddServiceInstancesToLastPackage(const aos::monitoring::NodeMonitoringData& monitoringData) const
{
    const auto& last = mMonitoring.back();

    size_t instanceToPushBack = 0;

    for (const auto& instanceMonitoring : monitoringData.mServiceInstances) {
        const auto itInstances
            = last.mServiceInstances.FindIf([&instanceMonitoring, &monitoringData](const auto& instance) {
                  return instance.mInstanceIdent == instanceMonitoring.mInstanceIdent
                      && instance.mNodeID == monitoringData.mNodeID;
              });

        if (itInstances == last.mServiceInstances.end()) {
            ++instanceToPushBack;

            continue;
        }

        if (itInstances->mItems.IsFull()) {
            return false;
        }
    }

    return last.mServiceInstances.Size() + instanceToPushBack <= last.mServiceInstances.MaxSize();
}

void Monitoring::AdjustMonitoringCache(const aos::monitoring::NodeMonitoringData& monitoringData)
{
    if (mMonitoring.empty()) {
        mMonitoring.emplace_back();

        return;
    }

    if (CanAddNodesToLastPackage(monitoringData.mNodeID) && CanAddServiceInstancesToLastPackage(monitoringData)) {
        return;
    }

    mMonitoring.emplace_back();

    if (!mIsConnected) {
        while (mMonitoring.size() > static_cast<size_t>(mConfig.mMaxOfflineMessages)) {
            mMonitoring.erase(mMonitoring.begin());
        }
    }
}

Error Monitoring::FillNodeMonitoring(
    const String& nodeID, const Time& timestamp, const aos::monitoring::NodeMonitoringData& nodeMonitoring)
{
    auto it = mMonitoring.back().mNodes.FindIf([&nodeID](const auto& node) { return node.mNodeID == nodeID; });
    if (it == mMonitoring.back().mNodes.end()) {
        auto err = mMonitoring.back().mNodes.EmplaceBack();
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        it = std::prev(mMonitoring.back().mNodes.end());
    }

    it->mNodeID = nodeID;

    if (auto err = it->mItems.EmplaceBack(); !err.IsNone()) {
        return err;
    }

    if (auto err = CreateMonitoringData(nodeMonitoring.mMonitoringData, timestamp, it->mItems.Back()); !err.IsNone()) {
        it->mItems.PopBack();

        return err;
    }

    return ErrorEnum::eNone;
}

Error Monitoring::FillInstanceMonitoring(
    const String& nodeID, const Time& timestamp, const aos::monitoring::InstanceMonitoringData& instanceMonitoring)
{
    auto& serviceInstances = mMonitoring.back().mServiceInstances;

    auto it = serviceInstances.FindIf([&instanceMonitoring, &nodeID](const auto& instance) {
        return instance.mInstanceIdent == instanceMonitoring.mInstanceIdent && instance.mNodeID == nodeID;
    });

    if (it == serviceInstances.end()) {
        if (auto err = serviceInstances.EmplaceBack(); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        it                 = std::prev(serviceInstances.end());
        it->mInstanceIdent = instanceMonitoring.mInstanceIdent;
        it->mNodeID        = nodeID;
    }

    if (auto err = it->mItems.EmplaceBack(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = CreateMonitoringData(instanceMonitoring.mMonitoringData, timestamp, it->mItems.Back());
        !err.IsNone()) {
        it->mItems.PopBack();

        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error Monitoring::CacheMonitoringData(const aos::monitoring::NodeMonitoringData& monitoringData)
{
    AdjustMonitoringCache(monitoringData);

    if (auto err = FillNodeMonitoring(monitoringData.mNodeID, monitoringData.mTimestamp, monitoringData);
        !err.IsNone()) {
        return err;
    }

    for (const auto& instanceMonitoring : monitoringData.mServiceInstances) {
        if (auto err = FillInstanceMonitoring(monitoringData.mNodeID, monitoringData.mTimestamp, instanceMonitoring);
            !err.IsNone()) {
            return err;
        }
    }

    return ErrorEnum::eNone;
}

void Monitoring::ProcessMonitoring(Poco::Timer&)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Process monitoring";

    if (!mIsRunning || !mIsConnected || mMonitoring.empty()) {
        return;
    }

    for (const auto& monitoring : mMonitoring) {
        auto msg = std::make_unique<cloudprotocol::MessageVariant>(monitoring);

        if (auto err = mCommunication->SendMessage(*msg); !err.IsNone()) {
            LOG_ERR() << "Can't send monitoring data" << Log::Field(err);
        }
    }

    mMonitoring.clear();
}

} // namespace aos::cm::monitoring
