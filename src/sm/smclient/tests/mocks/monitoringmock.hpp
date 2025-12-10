/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_SMCLIENT_TESTS_MOCKS_MONITORINGMOCK_HPP_
#define AOS_SM_SMCLIENT_TESTS_MOCKS_MONITORINGMOCK_HPP_

#include <gmock/gmock.h>

#include <core/common/monitoring/itf/monitoring.hpp>

namespace aos::monitoring {

class MonitoringMock : public MonitoringItf {
public:
    MOCK_METHOD(Error, GetAverageMonitoringData, (NodeMonitoringData&), (override));
};

} // namespace aos::monitoring

#endif
