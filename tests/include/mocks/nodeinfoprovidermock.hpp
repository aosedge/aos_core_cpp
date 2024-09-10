/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef NODEINFOPROVIDER_MOCK_HPP_
#define NODEINFOPROVIDER_MOCK_HPP_

#include <gmock/gmock.h>

#include <aos/iam/nodeinfoprovider.hpp>

/**
 * Node status observer mock.
 */
class NodeStatusObserverMock : public aos::iam::nodeinfoprovider::NodeStatusObserverItf {
public:
    MOCK_METHOD(
        aos::Error, OnNodeStatusChanged, (const aos::String& nodeID, const aos::NodeStatus& status), (override));
};

/**
 * Node info provider mock.
 */
class NodeInfoProviderMock : public aos::iam::nodeinfoprovider::NodeInfoProviderItf {
public:
    MOCK_METHOD(aos::Error, GetNodeInfo, (aos::NodeInfo&), (const, override));
    MOCK_METHOD(aos::Error, SetNodeStatus, (const aos::NodeStatus&), (override));
    MOCK_METHOD(
        aos::Error, SubscribeNodeStatusChanged, (aos::iam::nodeinfoprovider::NodeStatusObserverItf&), (override));
    MOCK_METHOD(
        aos::Error, UnsubscribeNodeStatusChanged, (aos::iam::nodeinfoprovider::NodeStatusObserverItf&), (override));
};

#endif
