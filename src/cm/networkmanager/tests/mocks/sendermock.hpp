/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_NETWORKMANAGER_TESTS_MOCKS_SENDERMOCK_HPP_
#define AOS_CM_NETWORKMANAGER_TESTS_MOCKS_SENDERMOCK_HPP_

#include <gmock/gmock.h>

#include <cm/networkmanager/itf/sender.hpp>

namespace aos::cm::networkmanager::tests {

class MockSender : public SenderItf {
public:
    MOCK_METHOD(Error, SendNetwork,
        (const std::string& nodeID, const std::vector<NetworkParameters>& networkParameters), (override));
};

} // namespace aos::cm::networkmanager::tests

#endif
