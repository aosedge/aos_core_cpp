/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <fstream>
#include <numeric>
#include <regex>
#include <thread>
#include <vector>

#include <sys/statvfs.h>

#include <common/utils/exception.hpp>
#include <core/common/tools/logger.hpp>

#include "nodemonitoringprovider.hpp"

namespace aos::sm::monitoring {

namespace {

/***********************************************************************************************************************
 * Consts
 **********************************************************************************************************************/

const auto cUnitMapping = std::map<std::string, size_t> {
    {"B", 1},
    {"KB", cKilobyte},
    {"MB", cKilobyte* cKilobyte},
    {"GB", cKilobyte* cKilobyte* cKilobyte},
    {"TB", cKilobyte* cKilobyte* cKilobyte* cKilobyte},
};

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error NodeMonitoringProvider::Init(
    aos::iamclient::CurrentNodeInfoProviderItf& nodeInfoProvider, sm::networkmanager::TrafficMonitorItf& trafficMonitor)
{
    LOG_DBG() << "Init node monitoring provider";

    mCPUCount         = std::thread::hardware_concurrency();
    mNodeInfoProvider = &nodeInfoProvider;
    mTrafficMonitor   = &trafficMonitor;

    return ErrorEnum::eNone;
}

Error NodeMonitoringProvider::Start()
{
    LOG_DBG() << "Start node monitoring provider";

    if (auto err = mNodeInfoProvider->GetCurrentNodeInfo(mNodeInfo); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error NodeMonitoringProvider::Stop()
{
    LOG_DBG() << "Stop node monitoring provider";

    return ErrorEnum::eNone;
}

Error NodeMonitoringProvider::GetNodeMonitoringData(MonitoringData& monitoringData)
{
    LOG_DBG() << "Get node monitoring data";

    Error err = ErrorEnum::eNone;

    if (Tie(monitoringData.mCPU, err) = GetSystemCPUUsage(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (Tie(monitoringData.mRAM, err) = GetSystemRAMUsage(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    LOG_DBG() << "Get node monitoring data" << Log::Field("cpu", monitoringData.mCPU)
              << Log::Field("ram", monitoringData.mRAM / cKilobyte);

    for (const auto& partition : mNodeInfo.mPartitions) {
        err = monitoringData.mPartitions.EmplaceBack();
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        err = monitoringData.mPartitions.Back().mName.Assign(partition.mName);
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (Tie(monitoringData.mPartitions.Back().mUsedSize, err) = GetSystemDiskUsage(partition.mPath);
            !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        LOG_DBG() << "Get node monitoring data" << Log::Field("name", partition.mName)
                  << Log::Field("usedSize", monitoringData.mPartitions.Back().mUsedSize / cKilobyte);
    }

    if (mTrafficMonitor) {
        if (err = mTrafficMonitor->GetSystemData(monitoringData.mDownload, monitoringData.mUpload); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        LOG_DBG() << "Get node monitoring data" << Log::Field("download(K)", monitoringData.mDownload / cKilobyte)
                  << Log::Field("upload(K)", monitoringData.mUpload / cKilobyte);
    }

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

RetWithError<double> NodeMonitoringProvider::GetSystemCPUUsage()
{
    constexpr auto cCPUTag             = std::string_view("cpu  ");
    constexpr auto cCPUUsageDelimiter  = ' ';
    constexpr auto cCPUIdleIndex       = 3;
    constexpr auto cCPUUsageMinEntries = 4;

    std::ifstream file(cSysCPUUsageFile);
    if (!file.is_open()) {
        return {0, AOS_ERROR_WRAP(ErrorEnum::eFailed)};
    }

    // Skip the 'cpu' prefix.
    file.ignore(cCPUTag.length(), cCPUUsageDelimiter);

    std::vector<size_t> stats;

    for (size_t entry = 0; file >> entry; stats.push_back(entry)) { }

    if (stats.size() < cCPUUsageMinEntries) {
        return {0, AOS_ERROR_WRAP(ErrorEnum::eFailed)};
    }

    const auto currentCPUUsage = CPUUsage {
        stats[cCPUIdleIndex], std::accumulate(stats.begin(), stats.end(), static_cast<size_t>(0)), Time::Now()};

    const auto   idleTimeDelta  = static_cast<double>(currentCPUUsage.mIdle - mPrevSysCPUUsage.mIdle);
    const auto   totalTimeDelta = static_cast<double>(currentCPUUsage.mTotal - mPrevSysCPUUsage.mTotal);
    const double utilization    = 100.0 * double(1.0 - idleTimeDelta / totalTimeDelta);

    mPrevSysCPUUsage = currentCPUUsage;

    return {utilization, ErrorEnum::eNone};
}

RetWithError<size_t> NodeMonitoringProvider::GetSystemRAMUsage()
{
    static const auto cSysCPURegex = std::regex(R"((\w+):\s+(\d+)\s+(\w+))");

    std::ifstream file(cMemInfoFile);
    if (!file.is_open()) {
        return {0, AOS_ERROR_WRAP(ErrorEnum::eFailed)};
    }

    size_t      totalRAM     = 0;
    size_t      freeRAM      = 0;
    size_t      buffers      = 0;
    size_t      cached       = 0;
    size_t      sReclaimable = 0;
    std::string line;

    while (std::getline(file, line)) {
        std::smatch match;

        if (std::regex_match(line, match, cSysCPURegex)) {
            std::string name  = match[1];
            size_t      value = std::stoull(match[2]);

            std::string unit = match[3];
            std::transform(unit.begin(), unit.end(), unit.begin(), ::toupper);

            if (auto it = cUnitMapping.find(unit); it != cUnitMapping.end()) {
                value *= it->second;
            }

            if (name == "MemTotal") {
                totalRAM = value;
            } else if (name == "MemFree") {
                freeRAM = value;
            } else if (name == "Buffers") {
                buffers = value;
            } else if (name == "Cached") {
                cached = value;
            } else if (name == "SReclaimable") {
                sReclaimable = value;
            }
        }
    }

    const size_t used = totalRAM - freeRAM - buffers - cached - sReclaimable;

    if (used > totalRAM) {
        return {0, AOS_ERROR_WRAP(ErrorEnum::eFailed)};
    }

    return {used, ErrorEnum::eNone};
}

RetWithError<uint64_t> NodeMonitoringProvider::GetSystemDiskUsage(const String& path)
{
    struct statvfs sbuf;

    if (auto ret = statvfs(path.CStr(), &sbuf); ret != 0) {
        return {0, AOS_ERROR_WRAP(Error(ret, "failed to get disk usage"))};
    }

    return {static_cast<uint64_t>(sbuf.f_blocks - sbuf.f_bfree) * static_cast<uint64_t>(sbuf.f_frsize)};
}

}; // namespace aos::sm::monitoring
