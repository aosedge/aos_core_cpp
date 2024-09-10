/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef NODEMANAGER_MOCK_HPP_
#define NODEMANAGER_MOCK_HPP_

#include <gmock/gmock.h>

#include <aos/iam/nodemanager.hpp>

/**
 * Node manager mock.
 */
class NodeManagerMock : public aos::iam::nodemanager::NodeManagerItf {
public:
    MOCK_METHOD(aos::Error, SetNodeInfo, (const aos::NodeInfo&), (override));
    MOCK_METHOD(aos::Error, SetNodeStatus, (const aos::String&, aos::NodeStatus), (override));
    MOCK_METHOD(aos::Error, GetNodeInfo, (const aos::String&, aos::NodeInfo&), (const, override));
    MOCK_METHOD(aos::Error, GetAllNodeIds, (aos::Array<aos::StaticString<aos::cNodeIDLen>>&), (const, override));
    MOCK_METHOD(aos::Error, RemoveNodeInfo, (const aos::String&), (override));
    MOCK_METHOD(aos::Error, SubscribeNodeInfoChange, (aos::iam::nodemanager::NodeInfoListenerItf&), (override));
};

#endif
