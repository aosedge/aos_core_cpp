/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef NODEINFOPROVIDER_HPP_
#define NODEINFOPROVIDER_HPP_

#include <mutex>
#include <string>
#include <unordered_set>

#include <aos/iam/nodeinfoprovider.hpp>

#include "config/config.hpp"

namespace aos::iam::nodeinfoprovider {

/**
 * Node info provider.
 */
class NodeInfoProvider : public iam::nodeinfoprovider::NodeInfoProviderItf {
public:
    /**
     * Initializes the node info provider.
     *
     * @param config node configuration
     * @return Error
     */
    Error Init(const iam::config::NodeInfoConfig& config);

    /**
     * Gets the node info object.
     *
     * @param[out] nodeInfo node info
     * @return Error
     */
    Error GetNodeInfo(NodeInfo& nodeInfo) const override;

    /**
     * Sets the node status.
     *
     * @param status node status
     * @return Error
     */
    Error SetNodeStatus(const NodeStatus& status) override;

    /**
     * Subscribes on node status changed event.
     *
     * @param observer node status changed observer
     * @return Error
     */
    Error SubscribeNodeStatusChanged(iam::nodeinfoprovider::NodeStatusObserverItf& observer) override;

    /**
     * Unsubscribes from node status changed event.
     *
     * @param observer node status changed observer
     * @return Error
     */
    Error UnsubscribeNodeStatusChanged(iam::nodeinfoprovider::NodeStatusObserverItf& observer) override;

private:
    Error InitAtrributesInfo(const iam::config::NodeInfoConfig& config);
    Error InitPartitionInfo(const iam::config::NodeInfoConfig& config);
    Error NotifyNodeStatusChanged();

    mutable std::mutex                                                mMutex;
    std::unordered_set<iam::nodeinfoprovider::NodeStatusObserverItf*> mObservers;
    std::string                                                       mMemInfoPath;
    std::string                                                       mProvisioningStatusPath;
    NodeInfo                                                          mNodeInfo;
};

} // namespace aos::iam::nodeinfoprovider

#endif
