/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_IAMCLIENT_TESTS_MOCKS_NODESLISTENERMOCK_HPP_
#define AOS_COMMON_IAMCLIENT_TESTS_MOCKS_NODESLISTENERMOCK_HPP_

#include <gmock/gmock.h>

#include <core/common/iamclient/itf/nodeinfoprovider.hpp>

/**
 * Mock for NodeInfoListenerItf (for nodes service).
 */
class NodesListenerMock : public aos::iamclient::NodeInfoListenerItf {
public:
    MOCK_METHOD(void, OnNodeInfoChanged, (const aos::NodeInfo& nodeInfo), (override));
};

#endif
