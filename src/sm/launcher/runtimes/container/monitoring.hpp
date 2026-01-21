/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_CONTAINER_MONITORING_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_CONTAINER_MONITORING_HPP_

#include <unordered_map>

#include "itf/monitoring.hpp"

namespace aos::sm::launcher {

/**
 * Monitoring interface.
 */
class Monitoring : public MonitoringItf {
public:
    /**
     * Constructor.
     */
    Monitoring();

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

private:
    static constexpr auto cCgroupsPath  = "/sys/fs/cgroup/system.slice/system-aos\\x2dservice.slice";
    static constexpr auto cCpuUsageFile = "cpu.stat";
    static constexpr auto cMemUsageFile = "memory.current";

    struct CPUUsage {
        size_t    mIdle      = 0;
        size_t    mTotal     = 0;
        aos::Time mTimestamp = Time::Now();
    };

    double GetInstanceCPUUsage(const std::string& instanceID);
    size_t GetInstanceCPUUSec(const std::string& instanceID);
    size_t GetInstanceRAMUsage(const std::string& instanceID);

    size_t                                    mCPUCount;
    std::unordered_map<std::string, CPUUsage> mInstanceMonitoringCache;
};

} // namespace aos::sm::launcher

#endif
