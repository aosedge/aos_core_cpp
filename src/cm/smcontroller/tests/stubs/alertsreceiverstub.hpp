/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_SMCONTROLLER_TESTS_STUBS_ALERTS_RECEIVERSTUB_HPP_
#define AOS_CM_SMCONTROLLER_TESTS_STUBS_ALERTS_RECEIVERSTUB_HPP_

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <vector>

#include <core/cm/alerts/itf/receiver.hpp>

namespace aos::cm::alerts {

/**
 * Alerts receiver stub.
 */
class ReceiverStub : public ReceiverItf {
public:
    Error OnAlertReceived(const AlertVariant& alert) override
    {
        std::lock_guard lock {mMutex};

        mAlerts.push_back(alert.GetValue<SystemAlert>());
        mCV.notify_all();

        return ErrorEnum::eNone;
    }

    Error WaitAlert(const String& nodeID)
    {
        std::unique_lock lock {mMutex};

        auto hasAlert = [this, &nodeID]() {
            auto it = std::find_if(mAlerts.begin(), mAlerts.end(),
                [&nodeID](const SystemAlert& alert) { return alert.mNodeID == nodeID; });

            return it != mAlerts.end();
        };

        bool received = mCV.wait_for(lock, cDefaultTimeout, hasAlert);

        if (!received) {
            return AOS_ERROR_WRAP(Error(ErrorEnum::eTimeout, "wait alert timeout"));
        }

        return ErrorEnum::eNone;
    }

    SystemAlert GetLatestAlert(const String& nodeID) const
    {
        std::lock_guard lock {mMutex};

        auto it = std::find_if(
            mAlerts.rbegin(), mAlerts.rend(), [&nodeID](const SystemAlert& alert) { return alert.mNodeID == nodeID; });

        if (it != mAlerts.rend()) {
            return *it;
        }

        return {};
    }

private:
    static constexpr auto cDefaultTimeout = std::chrono::seconds(1);

    std::vector<SystemAlert> mAlerts;
    mutable std::mutex       mMutex;
    std::condition_variable  mCV;
};

} // namespace aos::cm::alerts

#endif
