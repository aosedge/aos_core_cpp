/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef NODEINFOPROVIDER_HPP_
#define NODEINFOPROVIDER_HPP_

#include <string>

#include <aos/iam/nodeinfoprovider.hpp>

#include "config/config.hpp"

/**
 * Node info provider.
 */
class NodeInfoProvider : public aos::iam::NodeInfoProviderItf {
public:
    /**
     * Initializes the node info provider.
     *
     * @param config node configuration
     * @return Error
     */
    aos::Error Init(const NodeInfoConfig& config);

    /**
     * Gets the node info object.
     *
     * @param[out] nodeInfo node info
     * @return Error
     */
    aos::Error GetNodeInfo(aos::NodeInfo& nodeInfo) const override;

    /**
     * Sets the node status.
     *
     * @param status node status
     * @return Error
     */
    aos::Error SetNodeStatus(const aos::NodeStatus& status) override;

private:
    aos::Error InitAtrributesInfo(const NodeInfoConfig& config);
    aos::Error InitPartitionInfo(const NodeInfoConfig& config);

    std::string   mMemInfoPath;
    std::string   mProvisioningStatusPath;
    aos::NodeInfo mNodeInfo;
};

#endif
