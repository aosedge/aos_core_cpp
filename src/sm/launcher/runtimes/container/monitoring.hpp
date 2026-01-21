/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_CONTAINER_MONITORING_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_CONTAINER_MONITORING_HPP_

#include "itf/monitoring.hpp"

namespace aos::sm::launcher {

/**
 * Monitoring interface.
 */
class Monitoring : public MonitoringItf {
public:
    /**
     * Starts instance monitoring.
     *
     * @param instanceID instance ID.
     * @return Error.
     */
    Error StartInstanceMonitoring(const std::string& instanceID) override;

    /**
     * Stops instance monitoring.
     *
     * @param instanceID instance ID.
     * @return Error.
     */
    Error StopInstanceMonitoring(const std::string& instanceID) override;

    /**
     * Returns instance monitoring data.
     *
     * @param instanceID instance ID.
     * @param[out] monitoringData instance monitoring data.
     * @return Error.
     */
    Error GetInstanceMonitoringData(
        const std::string& instanceID, monitoring::InstanceMonitoringData& monitoringData) override;
};

} // namespace aos::sm::launcher

#endif
