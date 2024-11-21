/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef RESOURCEMONITOR_MOCK_HPP_
#define RESOURCEMONITOR_MOCK_HPP_

#include <gmock/gmock.h>

#include <aos/common/monitoring/resourcemonitor.hpp>

/**
 * Resource monitor mock.
 */
class ResourceMonitorMock : public aos::monitoring::ResourceMonitorItf {
public:
    MOCK_METHOD(aos::Error, StartInstanceMonitoring,
        (const aos::String&, const aos::monitoring::InstanceMonitorParams&), (override));
    MOCK_METHOD(aos::Error, StopInstanceMonitoring, (const aos::String&), (override));
    MOCK_METHOD(aos::Error, GetAverageMonitoringData, (aos::monitoring::NodeMonitoringData&), (override));
};

#endif
