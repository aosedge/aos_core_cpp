/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_SMCLIENT_TESTS_MOCKS_NODECONFIGHANDLERMOCK_HPP_
#define AOS_SM_SMCLIENT_TESTS_MOCKS_NODECONFIGHANDLERMOCK_HPP_

#include <gmock/gmock.h>

#include <core/sm/nodeconfig/itf/nodeconfighandler.hpp>

namespace aos::sm::nodeconfig {

class NodeConfigHandlerMock : public NodeConfigHandlerItf {
public:
    MOCK_METHOD(Error, CheckNodeConfig, (const NodeConfig&), (override));
    MOCK_METHOD(Error, UpdateNodeConfig, (const NodeConfig&), (override));
    MOCK_METHOD(Error, GetNodeConfigStatus, (NodeConfigStatus&), (override));
};

} // namespace aos::sm::nodeconfig

#endif
