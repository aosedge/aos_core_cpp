/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef RESOURCEUSAGEPROVIDER_HPP_
#define RESOURCEUSAGEPROVIDER_HPP_

#include <map>

#include <aos/common/monitoring/monitoring.hpp>
#include <aos/sm/networkmanager.hpp>

namespace aos::sm::monitoring {

/**
 * Resource usage provider interface.
 */
class ResourceUsageProvider : public aos::monitoring::ResourceUsageProviderItf {
public:
    /**
     * Constructor.
     *
     */
    ResourceUsageProvider();

    /**
     * Initializes resource usage provider.
     *
     * @param networkManager network manager.
     * @return Error.
     */
    Error Init(sm::networkmanager::NetworkManagerItf& networkManager);

    /**
     * Returns node monitoring data.
     *
     * @param nodeID node ident.
     * @param[out] monitoringData monitoring data.
     * @return Error.
     */
    Error GetNodeMonitoringData(const String& nodeID, aos::monitoring::MonitoringData& monitoringData) override;

    /**
     * Returns instance monitoring data.
     *
     * @param instanceID instance ID.
     * @param[out] monitoringData monitoring data.
     * @return Error.
     */
    Error GetInstanceMonitoringData(
        const String& instanceID, aos::monitoring::InstanceMonitoringData& monitoringData) override;

private:
    static constexpr auto cSysCPUUsageFile = "/proc/stat";
    static constexpr auto cMemInfoFile     = "/proc/meminfo";
    static constexpr auto cCgroupsPath     = "/sys/fs/cgroup/system.slice/system-aos\\x2dservice.slice";
    static constexpr auto cCpuUsageFile    = "cpu.stat";
    static constexpr auto cMemUsageFile    = "memory.current";

    struct CPUUsage {
        size_t    mIdle      = 0;
        size_t    mTotal     = 0;
        aos::Time mTimestamp = aos::Time::Now();
    };

    RetWithError<double>   GetSystemCPUUsage();
    RetWithError<size_t>   GetSystemRAMUsage();
    RetWithError<uint64_t> GetSystemDiskUsage(const String& path);
    RetWithError<size_t>   GetInstanceCPUUsage(const String& instanceID);
    RetWithError<size_t>   GetInstanceRAMUsage(const String& instanceID);
    RetWithError<uint64_t> GetInstanceDiskUsage(const String& path, uint32_t uid);
    Error SetInstanceMonitoringData(const String& instanceID, aos::monitoring::MonitoringData& monitoringData);

    sm::networkmanager::NetworkManagerItf* mNetworkManager = nullptr;
    CPUUsage                               mPrevSysCPUUsage;
    size_t                                 mCPUCount = 0;
    std::map<std::string, CPUUsage>        mInstanceMonitoringCache;
    std::map<std::string, std::string>     mPathToDeviceCache;
};

} // namespace aos::sm::monitoring

#endif
