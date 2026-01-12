/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_COMMUNICATION_TESTS_MOCKS_SMCONTROLLERMOCK_HPP_
#define AOS_CM_COMMUNICATION_TESTS_MOCKS_SMCONTROLLERMOCK_HPP_

#include <gmock/gmock.h>

#include <core/cm/smcontroller/itf/logprovider.hpp>

namespace aos::cm::smcontroller {

/**
 * Log provider mock.
 */
class LogProviderMock : public LogProviderItf {
public:
    MOCK_METHOD(Error, RequestLog, (const aos::RequestLog&), (override));
};

} // namespace aos::cm::smcontroller

#endif
