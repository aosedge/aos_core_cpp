/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_SMCONTROLLER_TESTS_STUBS_MONITORING_RECEIVERSTUB_HPP_
#define AOS_CM_SMCONTROLLER_TESTS_STUBS_MONITORING_RECEIVERSTUB_HPP_

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <vector>

#include <core/cm/monitoring/itf/receiver.hpp>

namespace aos::cm::monitoring {

/**
 * Monitoring receiver stub.
 */
class ReceiverStub : public ReceiverItf {
public:
    Error OnMonitoringReceived(const aos::monitoring::NodeMonitoringData& monitoring) override
    {
        std::lock_guard lock {mMutex};

        mMonitoringData.push_back(monitoring);
        mCV.notify_all();

        return ErrorEnum::eNone;
    }

    Error WaitMonitoringData(const String& nodeID, const InstanceIdent& instanceIdent)
    {
        std::unique_lock lock {mMutex};

        auto hasMonitoring = [this, &nodeID, &instanceIdent]() {
            return FindInstanceMonitoringDataLocked(nodeID, instanceIdent) != nullptr;
        };

        bool received = mCV.wait_for(lock, cDefaultTimeout, hasMonitoring);

        if (!received) {
            return AOS_ERROR_WRAP(Error(ErrorEnum::eTimeout, "wait monitoring data timeout"));
        }

        return ErrorEnum::eNone;
    }

    aos::monitoring::InstanceMonitoringData GetInstanceMonitoringData(
        const String& nodeID, const InstanceIdent& instanceIdent) const
    {
        std::lock_guard lock {mMutex};

        const auto* instData = FindInstanceMonitoringDataLocked(nodeID, instanceIdent);
        if (instData == nullptr) {
            return aos::monitoring::InstanceMonitoringData {};
        }

        return *instData;
    }

private:
    const aos::monitoring::InstanceMonitoringData* FindInstanceMonitoringDataLocked(
        const String& nodeID, const InstanceIdent& instanceIdent) const
    {
        auto it = std::find_if(mMonitoringData.begin(), mMonitoringData.end(),
            [&nodeID](const aos::monitoring::NodeMonitoringData& data) { return data.mNodeID == nodeID; });

        if (it == mMonitoringData.end()) {
            return nullptr;
        }

        auto instIt = std::find_if(it->mInstances.begin(), it->mInstances.end(),
            [&instanceIdent](const aos::monitoring::InstanceMonitoringData& instData) {
                return instData.mInstanceIdent == instanceIdent;
            });

        if (instIt == it->mInstances.end()) {
            return nullptr;
        }

        return &(*instIt);
    }

    static constexpr auto cDefaultTimeout = std::chrono::seconds(1);

    std::vector<aos::monitoring::NodeMonitoringData> mMonitoringData;
    mutable std::mutex                               mMutex;
    std::condition_variable                          mCV;
};

} // namespace aos::cm::monitoring

#endif
