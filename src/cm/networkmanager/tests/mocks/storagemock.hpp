/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_NETWORKMANAGER_TESTS_MOCKS_STORAGEMOCK_HPP_
#define AOS_CM_NETWORKMANAGER_TESTS_MOCKS_STORAGEMOCK_HPP_

#include <gmock/gmock.h>

#include <cm/networkmanager/itf/storage.hpp>

namespace aos::cm::networkmanager::tests {

class MockStorage : public StorageItf {
public:
    MOCK_METHOD(Error, AddNetwork, (const Network& network), (override));
    MOCK_METHOD(Error, AddHost, (const String& networkID, const Host& host), (override));
    MOCK_METHOD(Error, AddInstance, (const Instance& instance), (override));
    MOCK_METHOD(Error, GetNetworks, (Array<Network> & networks), (override));
    MOCK_METHOD(Error, GetHosts, (const String& networkID, Array<Host>& hosts), (override));
    MOCK_METHOD(
        Error, GetInstances, (const String& networkID, const String& nodeID, Array<Instance>& instances), (override));
    MOCK_METHOD(Error, RemoveNetwork, (const String& networkID), (override));
    MOCK_METHOD(Error, RemoveHost, (const String& networkID, const String& nodeID), (override));
    MOCK_METHOD(Error, RemoveInstance, (const InstanceIdent& instanceIdent), (override));
};

} // namespace aos::cm::networkmanager::tests

#endif
