/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_SMCONTROLLER_TESTS_STUBS_INSTANCESTATUSRECEIVERSTUB_HPP_
#define AOS_CM_SMCONTROLLER_TESTS_STUBS_INSTANCESTATUSRECEIVERSTUB_HPP_

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <vector>

#include <core/cm/launcher/itf/instancestatusreceiver.hpp>

namespace aos::cm::launcher {

/**
 * Instance status receiver stub.
 */
class InstanceStatusReceiverStub : public InstanceStatusReceiverItf {
public:
    Error OnInstanceStatusReceived(const InstanceStatus& status) override
    {
        StaticArray<InstanceStatus, 1> statuses;

        statuses.PushBack(status);

        OnNodeInstancesStatusesReceived(status.mNodeID, statuses);

        return ErrorEnum::eNone;
    }

    Error OnNodeInstancesStatusesReceived(const String& nodeID, const Array<InstanceStatus>& statuses) override
    {
        std::lock_guard lock {mMutex};

        for (const auto& status : statuses) {
            NodeInstanceStatus nodeStatus;

            nodeStatus.mNodeID = nodeID;
            nodeStatus.mStatus = status;
            mNodeInstanceStatuses.push_back(nodeStatus);
        }

        mCV.notify_all();

        return ErrorEnum::eNone;
    }

    Error WaitInstanceStatus(const String& nodeID, const InstanceIdent& instanceIdent)
    {
        std::unique_lock lock {mMutex};

        auto hasStatus = [this, &nodeID, &instanceIdent]() {
            auto it = std::find_if(mNodeInstanceStatuses.begin(), mNodeInstanceStatuses.end(),
                [&nodeID, &instanceIdent](const NodeInstanceStatus& nodeStatus) {
                    return nodeStatus.mNodeID == nodeID
                        && static_cast<const InstanceIdent&>(nodeStatus.mStatus) == instanceIdent;
                });

            return it != mNodeInstanceStatuses.end();
        };

        bool received = mCV.wait_for(lock, cDefaultTimeout, hasStatus);
        if (!received) {
            return AOS_ERROR_WRAP(Error(ErrorEnum::eTimeout, "wait instance status timeout"));
        }

        return ErrorEnum::eNone;
    }

private:
    struct NodeInstanceStatus {
        StaticString<cIDLen> mNodeID;
        InstanceStatus       mStatus;
    };

    static constexpr auto cDefaultTimeout = std::chrono::seconds(1);

    std::vector<NodeInstanceStatus> mNodeInstanceStatuses;
    mutable std::mutex              mMutex;
    std::condition_variable         mCV;
};

} // namespace aos::cm::launcher

#endif
