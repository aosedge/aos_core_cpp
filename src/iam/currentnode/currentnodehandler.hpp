/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_IAM_CURRENTNODE_CURRENTNODEHANDLER_HPP_
#define AOS_IAM_CURRENTNODE_CURRENTNODEHANDLER_HPP_

#include <mutex>
#include <string>
#include <unordered_set>

#include <core/iam/currentnode/itf/currentnodehandler.hpp>

#include <iam/config/config.hpp>

namespace aos::iam::currentnode {

/**
 * Current node handler.
 */
class CurrentNodeHandler : public CurrentNodeHandlerItf {
public:
    /**
     * Initializes the node info provider.
     *
     * @param config node configuration
     * @return Error
     */
    Error Init(const iam::config::NodeInfoConfig& config);

    /**
     * Returns current node info    .
     *
     * @param[out] nodeInfo current node information.
     * @return Error.
     */
    Error GetCurrentNodeInfo(NodeInfo& nodeInfo) const override;

    /**
     * Subscribes current node info notifications.
     *
     * @param listener current node info listener.
     * @return Error.
     */
    Error SubscribeListener(iamclient::CurrentNodeInfoListenerItf& listener) override;

    /**
     * Unsubscribes from current node info notifications.
     *
     * @param listener current node info listener.
     * @return Error.
     */
    Error UnsubscribeListener(iamclient::CurrentNodeInfoListenerItf& listener) override;

    /**
     * Sets current node state.
     *
     * @param state new node state.
     * @return Error.
     */
    Error SetState(NodeState state) override;

    /**
     * Sets current node connected state.
     *
     * @param isConnected connected state.
     * @return Error.
     */
    Error SetConnected(bool isConnected) override;

private:
    Error InitCPUInfo(const iam::config::NodeInfoConfig& config);
    Error InitOSInfo(const iam::config::NodeInfoConfig& config);
    Error InitAtrributesInfo(const iam::config::NodeInfoConfig& config);
    Error InitPartitionInfo(const iam::config::NodeInfoConfig& config);
    Error ReadNodeState();
    void  NotifyNodeInfoChanged();
    Error UpdateProvisionFile(NodeState state);

    mutable std::mutex                                         mMutex;
    std::unordered_set<iamclient::CurrentNodeInfoListenerItf*> mListeners;
    std::string                                                mMemInfoPath;
    std::string                                                mProvisioningStatusPath;
    NodeInfo                                                   mNodeInfo;
};

} // namespace aos::iam::currentnode

#endif
