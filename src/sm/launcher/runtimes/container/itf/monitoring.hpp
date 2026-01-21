/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_CONTAINER_ITF_MONITORING_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_CONTAINER_ITF_MONITORING_HPP_

#include <string>
#include <vector>

#include <core/common/monitoring/itf/monitoringdata.hpp>

namespace aos::sm::launcher {

/**
 * Monitoring interface.
 */
class MonitoringItf {
public:
    /**
     * Destructor.
     */
    virtual ~MonitoringItf() = default;

    /**
     * Starts instance monitoring.
     *
     * @param instanceID instance ID.
     * @param uid instance user ID.
     * @param partInfos partition infos.
     * @return Error.
     */
    virtual Error StartInstanceMonitoring(
        const std::string& instanceID, uid_t uid, const std::vector<PartitionInfo>& partInfos)
        = 0;

    /**
     * Stops instance monitoring.
     *
     * @param instanceID instance ID.
     * @return Error.
     */
    virtual Error StopInstanceMonitoring(const std::string& instanceID) = 0;

    /**
     * Returns instance monitoring data.
     *
     * @param instanceID instance ID.
     * @param[out] monitoringData instance monitoring data.
     * @return Error.
     */
    virtual Error GetInstanceMonitoringData(
        const std::string& instanceID, monitoring::InstanceMonitoringData& monitoringData)
        = 0;
};

} // namespace aos::sm::launcher

#endif
