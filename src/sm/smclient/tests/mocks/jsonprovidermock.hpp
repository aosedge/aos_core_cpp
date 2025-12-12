/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_SMCLIENT_TESTS_MOCKS_JSONPROVIDERMOCK_HPP_
#define AOS_SM_SMCLIENT_TESTS_MOCKS_JSONPROVIDERMOCK_HPP_

#include <gmock/gmock.h>

#include <core/common/nodeconfig/itf/jsonprovider.hpp>

namespace aos::nodeconfig {

class JSONProviderMock : public JSONProviderItf {
public:
    MOCK_METHOD(Error, NodeConfigToJSON, (const NodeConfig& nodeConfig, String& json), (const, override));
    MOCK_METHOD(Error, NodeConfigFromJSON, (const String& json, NodeConfig& nodeConfig), (const, override));
};

} // namespace aos::nodeconfig

#endif
