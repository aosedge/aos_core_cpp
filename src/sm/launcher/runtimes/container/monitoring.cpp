/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <fstream>
#include <thread>

#include <core/common/tools/logger.hpp>

#include <common/utils/exception.hpp>
#include <common/utils/filesystem.hpp>
#include <common/utils/utils.hpp>

#include "monitoring.hpp"

namespace aos::sm::launcher {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Monitoring::Monitoring()
    : mCPUCount(std::thread::hardware_concurrency())
{
}

Error Monitoring::Init(networkmanager::InstanceTrafficProviderItf& trafficProvider)
{
    mTrafficProvider = &trafficProvider;

    return ErrorEnum::eNone;
}

Error Monitoring::StartInstanceMonitoring(
    const std::string& instanceID, uid_t uid, const std::vector<PartitionInfo>& partInfos)
{
    try {
        LOG_DBG() << "Start instance monitoring" << Log::Field("instanceID", instanceID.c_str());

        mInstanceMonitoringCache.insert_or_assign(instanceID, MonitoringData {{}, partInfos, uid});

        return ErrorEnum::eNone;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e, ErrorEnum::eRuntime));
    }
}

Error Monitoring::StopInstanceMonitoring(const std::string& instanceID)
{
    try {
        LOG_DBG() << "Stop instance monitoring" << Log::Field("instanceID", instanceID.c_str());

        mInstanceMonitoringCache.erase(instanceID);

        return ErrorEnum::eNone;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e, ErrorEnum::eRuntime));
    }
}

Error Monitoring::GetInstanceMonitoringData(
    const std::string& instanceID, monitoring::InstanceMonitoringData& monitoringData)
{
    try {
        monitoringData.mMonitoringData.mTimestamp = Time::Now();
        monitoringData.mMonitoringData.mCPU       = GetInstanceCPUUsage(instanceID);
        monitoringData.mMonitoringData.mRAM       = GetInstanceRAMUsage(instanceID);

        LOG_DBG() << "Get instance monitoring data" << Log::Field("instanceID", instanceID.c_str())
                  << Log::Field("cpu", monitoringData.mMonitoringData.mCPU)
                  << Log::Field("ram", monitoringData.mMonitoringData.mRAM / cKilobyte);

        const auto& cachedData = mInstanceMonitoringCache.at(instanceID);

        for (const auto& partition : cachedData.mPartInfos) {
            auto err = monitoringData.mMonitoringData.mPartitions.EmplaceBack();
            if (!err.IsNone()) {
                return AOS_ERROR_WRAP(err);
            }

            auto& partitionUsage = monitoringData.mMonitoringData.mPartitions.Back();

            partitionUsage.mName     = partition.mName;
            partitionUsage.mUsedSize = GetInstanceDiskUsage(partition.mPath.CStr(), cachedData.mUID);

            LOG_DBG() << "Get instance monitoring data" << Log::Field("instanceID", instanceID.c_str())
                      << Log::Field("partition", partition.mName)
                      << Log::Field("usedSize", partitionUsage.mUsedSize / cKilobyte);
        }

        if (auto err = mTrafficProvider->GetInstanceTraffic(
                instanceID.c_str(), monitoringData.mMonitoringData.mDownload, monitoringData.mMonitoringData.mUpload);
            !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        LOG_DBG() << "Get instance monitoring data" << Log::Field("instanceID", instanceID.c_str())
                  << Log::Field("download", monitoringData.mMonitoringData.mDownload / cKilobyte)
                  << Log::Field("upload", monitoringData.mMonitoringData.mUpload / cKilobyte);

        return ErrorEnum::eNone;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e, ErrorEnum::eRuntime));
    }
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

size_t Monitoring::GetInstanceCPUUSec(const std::string& instanceID)
{
    const auto cpuUsageFile = common::utils::JoinPath(cCgroupsPath, instanceID, cCpuUsageFile);

    std::ifstream file(cpuUsageFile);
    if (!file.is_open()) {
        AOS_ERROR_THROW(ErrorEnum::eNotFound, "can't find cpu usage file");
    }

    std::string line;

    while (getline(file, line)) {
        std::istringstream lineStream(line);

        std::string key;
        size_t      value = 0;

        if (lineStream >> key >> value) {
            if (key == "usage_usec") {
                return value;
            }
        }
    }

    AOS_ERROR_THROW(ErrorEnum::eNotFound, "can't find cpu usage");
}

double Monitoring::GetInstanceCPUUsage(const std::string& instanceID)
{
    auto& cpuUsage = mInstanceMonitoringCache.at(instanceID).mCPUUsage;
    auto  cpuUSec  = GetInstanceCPUUSec(instanceID);

    if (cpuUsage.mTotal > cpuUSec) {
        cpuUsage.mTotal = 0;
    }

    const auto now    = Time::Now();
    const auto delta  = static_cast<double>(now.Sub(cpuUsage.mTimestamp).Microseconds());
    double     result = 0.0;

    if (delta > 0 && mCPUCount > 0) {
        result = static_cast<double>(cpuUSec - cpuUsage.mTotal) * 100.0 / delta / static_cast<double>(mCPUCount);
    }

    cpuUsage.mTotal     = cpuUSec;
    cpuUsage.mTimestamp = now;

    return result;
}

size_t Monitoring::GetInstanceRAMUsage(const std::string& instanceID)
{
    const auto memUsageFile = common::utils::JoinPath(cCgroupsPath, instanceID, cMemUsageFile);

    std::ifstream file(memUsageFile);
    if (!file.is_open()) {
        AOS_ERROR_THROW(ErrorEnum::eNotFound, "can't find memory usage file");
    }

    std::string line;

    if (!getline(file, line)) {
        AOS_ERROR_THROW(ErrorEnum::eFailed, "can't read memory usage file");
    }

    return std::stoull(line);
}

size_t Monitoring::GetInstanceDiskUsage(const std::string& path, uid_t uid)
{
    auto [mountPoint, err] = common::utils::GetMountPoint(path);
    if (!err.IsNone()) {
        AOS_ERROR_THROW(AOS_ERROR_WRAP(err));
    }

    std::string quotaOutput;

    Tie(quotaOutput, err)
        = common::utils::ExecCommand({"quota", "-u", std::to_string(uid), "-w", "--filesystem", mountPoint});
    if (!err.IsNone()) {
        AOS_ERROR_THROW(AOS_ERROR_WRAP(err));
    }

    std::istringstream quotaStream(quotaOutput);
    std::string        line;

    // Skip header lines, find the data line starting with the filesystem
    while (std::getline(quotaStream, line)) {
        std::istringstream lineStream(line);
        std::string        filesystem;
        size_t             blocks = 0;

        if (lineStream >> filesystem >> blocks) {
            if (filesystem == "Filesystem") {
                continue; // Skip header line
            }

            return blocks * cKilobyte;
        }
    }

    AOS_ERROR_THROW(ErrorEnum::eNotFound, "can't parse quota output");
}

}; // namespace aos::sm::launcher
