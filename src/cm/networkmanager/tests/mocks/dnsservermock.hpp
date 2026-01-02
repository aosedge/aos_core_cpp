/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_NETWORKMANAGER_TESTS_MOCKS_DNSSERVERMOCK_HPP_
#define AOS_CM_NETWORKMANAGER_TESTS_MOCKS_DNSSERVERMOCK_HPP_

#include <gmock/gmock.h>

#include <cm/networkmanager/itf/dnsserver.hpp>

namespace aos::cm::networkmanager {

class MockDNSServer : public DNSServerItf {
public:
    MOCK_METHOD(Error, UpdateHostsFile, (const HostsMap& hosts), (override));
    MOCK_METHOD(Error, Restart, (), (override));
    MOCK_METHOD(std::string, GetIP, (), (const, override));
};

} // namespace aos::cm::networkmanager

#endif // AOS_CM_NETWORKMANAGER_TESTS_MOCKS_DNSSERVERMOCK_HPP_
