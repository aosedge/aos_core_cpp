/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_NETWORKMANAGER_TESTS_MOCKS_NODENETWORKMOCK_HPP_
#define AOS_CM_NETWORKMANAGER_TESTS_MOCKS_NODENETWORKMOCK_HPP_

#include <gmock/gmock.h>

#include <core/cm/networkmanager/itf/nodenetwork.hpp>

namespace aos::cm::networkmanager {

class MockNodeNetwork : public NodeNetworkItf {
public:
    MOCK_METHOD(Error, UpdateNetworks, (const String& nodeID, const Array<UpdateNetworkParameters>& networkParameters),
        (override));
};

} // namespace aos::cm::networkmanager

#endif
