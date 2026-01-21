/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_CONTAINER_TESTS_MOCKS_MONITORINGMOCK_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_CONTAINER_TESTS_MOCKS_MONITORINGMOCK_HPP_

#include <gmock/gmock.h>

#include <sm/launcher/runtimes/container/itf/monitoring.hpp>

namespace aos::sm::launcher {

/**
 * Monitoring mock.
 */
class MonitoringMock : public MonitoringItf {
public:
    MOCK_METHOD(Error, StartInstanceMonitoring, (const std::string&), (override));
    MOCK_METHOD(Error, StopInstanceMonitoring, (const std::string&), (override));
    MOCK_METHOD(
        Error, GetInstanceMonitoringData, (const std::string&, monitoring::InstanceMonitoringData&), (override));
};

} // namespace aos::sm::launcher

#endif
