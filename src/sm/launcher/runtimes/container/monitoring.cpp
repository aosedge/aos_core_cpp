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

#include "monitoring.hpp"

namespace aos::sm::launcher {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Monitoring::Monitoring()
    : mCPUCount(std::thread::hardware_concurrency())
{
}

Error Monitoring::StartInstanceMonitoring(const std::string& instanceID)
{
    try {
        LOG_DBG() << "Start instance monitoring" << Log::Field("instanceID", instanceID.c_str());

        mInstanceMonitoringCache.insert_or_assign(instanceID, CPUUsage());

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
    auto& cpuUsage = mInstanceMonitoringCache.at(instanceID);
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

}; // namespace aos::sm::launcher
