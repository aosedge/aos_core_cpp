/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CONNECTIONSUBSCRIBER_MOCK_HPP_
#define CONNECTIONSUBSCRIBER_MOCK_HPP_

#include <gmock/gmock.h>

#include <aos/common/connectionsubsc.hpp>

/**
 * Connection subscriber mock.
 */
class ConnectionSubscriberMock : public aos::ConnectionSubscriberItf {
public:
    MOCK_METHOD(void, OnConnect, (), (override));
    MOCK_METHOD(void, OnDisconnect, (), (override));
};

#endif
