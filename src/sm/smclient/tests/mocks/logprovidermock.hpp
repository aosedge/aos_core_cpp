/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_SMCLIENT_TESTS_MOCKS_LOGPROVIDERMOCK_HPP_
#define AOS_SM_SMCLIENT_TESTS_MOCKS_LOGPROVIDERMOCK_HPP_

#include <gmock/gmock.h>

#include <core/sm/logging/itf/logprovider.hpp>

namespace aos::sm::logging {

class LogProviderMock : public LogProviderItf {
public:
    MOCK_METHOD(Error, GetInstanceLog, (const RequestLog&), (override));
    MOCK_METHOD(Error, GetInstanceCrashLog, (const RequestLog&), (override));
    MOCK_METHOD(Error, GetSystemLog, (const RequestLog&), (override));
};

} // namespace aos::sm::logging

#endif
