/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_IAM_NODEINFOPROVIDER_NODEINFOPROVIDER_HPP_
#define AOS_IAM_NODEINFOPROVIDER_NODEINFOPROVIDER_HPP_

#include <mutex>
#include <string>
#include <unordered_set>

#include <core/iam/nodeinfoprovider/nodeinfoprovider.hpp>

#include <iam/config/config.hpp>

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
     * Sets the node state.
     *
     * @param state node state.
     * @param provisioned node provisioned flag.
     * @return Error.
     */
    Error SetNodeState(const NodeState& state, bool provisioned) override;

    /**
     * Subscribes on node state changed event.
     *
     * @param observer node state changed observer
     * @return Error
     */
    Error SubscribeNodeStateChanged(NodeStateObserverItf& observer) override;

    /**
     * Unsubscribes from node state changed event.
     *
     * @param observer node state changed observer
     * @return Error
     */
    Error UnsubscribeNodeStateChanged(NodeStateObserverItf& observer) override;

private:
    Error InitOSInfo(const iam::config::NodeInfoConfig& config);
    Error InitAtrributesInfo(const iam::config::NodeInfoConfig& config);
    Error InitPartitionInfo(const iam::config::NodeInfoConfig& config);
    Error ReadNodeState();
    Error NotifyNodeStateChanged();

    mutable std::mutex                                               mMutex;
    std::unordered_set<iam::nodeinfoprovider::NodeStateObserverItf*> mObservers;
    std::string                                                      mMemInfoPath;
    std::string                                                      mProvisioningStatusPath;
    NodeInfo                                                         mNodeInfo;
};

} // namespace aos::iam::nodeinfoprovider

#endif
