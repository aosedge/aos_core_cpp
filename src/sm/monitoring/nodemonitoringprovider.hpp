/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_MONITORING_NODEMONITORINGPROVIDER_HPP_
#define AOS_SM_MONITORING_NODEMONITORINGPROVIDER_HPP_

#include <core/common/iamclient/itf/currentnodeinfoprovider.hpp>
#include <core/common/monitoring/itf/nodemonitoringprovider.hpp>
#include <core/sm/networkmanager/itf/trafficmonitor.hpp>

namespace aos::sm::monitoring {

/**
 * Node monitoring provider.
 */
class NodeMonitoringProvider : public aos::monitoring::NodeMonitoringProviderItf {
public:
    /**
     * Initializes node monitoring provider.
     *
     * @param nodeInfoProvider current node info provider.
     * @param trafficMonitor traffic monitor.
     * @return Error.
     */
    Error Init(
        iamclient::CurrentNodeInfoProviderItf& nodeInfoProvider, sm::networkmanager::TrafficMonitorItf& trafficMonitor);

    /**
     * Starts node monitoring provider.
     *
     * @return Error.
     */
    Error Start();

    /**
     * Stops node monitoring provider.
     *
     * @return Error.
     */
    Error Stop();

    /**
     * Returns node monitoring data.
     *
     * @param[out] monitoringData monitoring data.
     * @return Error.
     */
    Error GetNodeMonitoringData(MonitoringData& monitoringData) override;

private:
    static constexpr auto cSysCPUUsageFile = "/proc/stat";
    static constexpr auto cMemInfoFile     = "/proc/meminfo";

    struct CPUUsage {
        size_t mIdle {};
        size_t mTotal {};
        Time   mTimestamp {Time::Now()};
    };

    RetWithError<double>   GetSystemCPUUsage();
    RetWithError<size_t>   GetSystemRAMUsage();
    RetWithError<uint64_t> GetSystemDiskUsage(const String& path);

    iamclient::CurrentNodeInfoProviderItf* mNodeInfoProvider {};
    sm::networkmanager::TrafficMonitorItf* mTrafficMonitor {};
    NodeInfo                               mNodeInfo;
    CPUUsage                               mPrevSysCPUUsage;
    size_t                                 mCPUCount {};
};

} // namespace aos::sm::monitoring

#endif
