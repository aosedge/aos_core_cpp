/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_IAMCLIENT_TESTS_MOCKS_NODEINFOLISTENERMOCK_HPP_
#define AOS_COMMON_IAMCLIENT_TESTS_MOCKS_NODEINFOLISTENERMOCK_HPP_

#include <gmock/gmock.h>

#include <core/common/iamclient/itf/currentnodeinfoprovider.hpp>

/**
 * Mock for CurrentNodeInfoListenerItf.
 */
class NodeInfoListenerMock : public aos::iamclient::CurrentNodeInfoListenerItf {
public:
    MOCK_METHOD(void, OnCurrentNodeInfoChanged, (const aos::NodeInfo& nodeInfo), (override));
};

#endif
