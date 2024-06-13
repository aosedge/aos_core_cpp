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
 * Node info provider stub.
 */
class NodeInfoProviderMock : public aos::iam::NodeInfoProviderItf {
public:
    MOCK_METHOD(aos::Error, GetNodeInfo, (aos::NodeInfo&), (const, override));
    MOCK_METHOD(aos::Error, SetNodeStatus, (const aos::NodeStatus&), (override));
};

#endif
