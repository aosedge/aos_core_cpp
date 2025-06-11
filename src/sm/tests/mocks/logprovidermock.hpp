/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LOGPROVIDER_MOCK_HPP_
#define LOGPROVIDER_MOCK_HPP_

#include <gmock/gmock.h>

#include <aos/sm/logprovider.hpp>
#include <logprovider/logprovider.hpp>

namespace aos::sm::logprovider {

/**
 * Instance id provider mock.
 */
class InstanceIDProviderMock : public InstanceIDProviderItf {
public:
    MOCK_METHOD(RetWithError<std::vector<std::string>>, GetInstanceIDs, (const cloudprotocol::InstanceFilter& filter),
        (override));
};

/**
 * Log observer mock.
 */
class LogObserverMock : public aos::sm::logprovider::LogObserverItf {
public:
    MOCK_METHOD(aos::Error, OnLogReceived, (const aos::cloudprotocol::PushLog&), (override));
};

/**
 * Log provider mock.
 */
class LogProviderMock : public aos::sm::logprovider::LogProviderItf {
public:
    MOCK_METHOD(aos::Error, GetInstanceLog, (const aos::cloudprotocol::RequestLog&), (override));
    MOCK_METHOD(aos::Error, GetInstanceCrashLog, (const aos::cloudprotocol::RequestLog&), (override));
    MOCK_METHOD(aos::Error, GetSystemLog, (const aos::cloudprotocol::RequestLog&), (override));
    MOCK_METHOD(aos::Error, Subscribe, (aos::sm::logprovider::LogObserverItf&), (override));
    MOCK_METHOD(aos::Error, Unsubscribe, (aos::sm::logprovider::LogObserverItf&), (override));
};

} // namespace aos::sm::logprovider

#endif
