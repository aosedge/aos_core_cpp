/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_TESTS_MOCKS_LOGPROVIDERMOCK_HPP_
#define AOS_SM_TESTS_MOCKS_LOGPROVIDERMOCK_HPP_

#include <gmock/gmock.h>

#include <core/sm/logprovider/logprovider.hpp>

#include <sm/logprovider/logprovider.hpp>

namespace aos::sm::logprovider {

/**
 * Instance id provider mock.
 */
class InstanceIDProviderMock : public InstanceIDProviderItf {
public:
    MOCK_METHOD(RetWithError<std::vector<std::string>>, GetInstanceIDs, (const InstanceFilter& filter), (override));
};

/**
 * Log observer mock.
 */
class LogObserverMock : public aos::sm::logprovider::LogObserverItf {
public:
    MOCK_METHOD(aos::Error, OnLogReceived, (const aos::PushLog&), (override));
};

/**
 * Log provider mock.
 */
class LogProviderMock : public aos::sm::logprovider::LogProviderItf {
public:
    MOCK_METHOD(aos::Error, GetInstanceLog, (const aos::RequestLog&), (override));
    MOCK_METHOD(aos::Error, GetInstanceCrashLog, (const aos::RequestLog&), (override));
    MOCK_METHOD(aos::Error, GetSystemLog, (const aos::RequestLog&), (override));
    MOCK_METHOD(aos::Error, Subscribe, (aos::sm::logprovider::LogObserverItf&), (override));
    MOCK_METHOD(aos::Error, Unsubscribe, (aos::sm::logprovider::LogObserverItf&), (override));
};

} // namespace aos::sm::logprovider

#endif
