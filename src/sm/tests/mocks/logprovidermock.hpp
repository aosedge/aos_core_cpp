/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_TESTS_MOCKS_LOGPROVIDERMOCK_HPP_
#define AOS_SM_TESTS_MOCKS_LOGPROVIDERMOCK_HPP_

#include <gmock/gmock.h>

#include <core/common/logging/itf/sender.hpp>
#include <core/sm/logging/itf/logprovider.hpp>

#include <sm/logprovider/logprovider.hpp>

namespace aos::sm::logprovider {

/**
 * Instance id provider mock.
 */
class InstanceIDProviderMock : public InstanceIDProviderItf {
public:
    MOCK_METHOD(Error, GetInstanceIDs, (const LogFilter& filter, std::vector<std::string>& instanceIDs), (override));
};

/**
 * Log sender mock.
 */
class LogSenderMock : public aos::logging::SenderItf {
public:
    MOCK_METHOD(aos::Error, SendLog, (const aos::PushLog&), (override));
};

/**
 * Log provider mock.
 */
class LogProviderMock : public aos::sm::logging::LogProviderItf {
public:
    MOCK_METHOD(aos::Error, GetInstanceLog, (const aos::RequestLog&), (override));
    MOCK_METHOD(aos::Error, GetInstanceCrashLog, (const aos::RequestLog&), (override));
    MOCK_METHOD(aos::Error, GetSystemLog, (const aos::RequestLog&), (override));
};

} // namespace aos::sm::logprovider

#endif
