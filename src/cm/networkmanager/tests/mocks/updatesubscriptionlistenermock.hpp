/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_NETWORKMANAGER_TESTS_MOCKS_UPDATESUBSCRIPTIONLISTENERMOCK_HPP_
#define AOS_CM_NETWORKMANAGER_TESTS_MOCKS_UPDATESUBSCRIPTIONLISTENERMOCK_HPP_

#include <gmock/gmock.h>

#include <cm/networkmanager/itf/updatesubscriptionlistener.hpp>

namespace aos::cm::networkmanager {

class MockUpdateSubscriptionListener : public UpdateSubscriptionListenerItf {
public:
    MOCK_METHOD(void, OnNodeSubscribed, (const String& nodeID), (override));
    MOCK_METHOD(void, OnNodeUnsubscribed, (const String& nodeID), (override));
};

} // namespace aos::cm::networkmanager

#endif // AOS_CM_NETWORKMANAGER_TESTS_MOCKS_UPDATESUBSCRIPTIONLISTENERMOCK_HPP_
