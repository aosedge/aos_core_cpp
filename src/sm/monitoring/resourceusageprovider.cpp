/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>
#include <functional>
#include <numeric>
#include <regex>
#include <thread>
#include <vector>

#include <linux/dqblk_xfs.h>
#include <linux/major.h>
#include <sys/quota.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/sysmacros.h>

#include <Poco/StringTokenizer.h>

#include <common/utils/exception.hpp>
#include <sm/logger/logmodule.hpp>

#include "resourceusageprovider.hpp"

namespace aos::sm::monitoring {

namespace {

constexpr size_t cKilobyte    = 1024;
const auto       cUnitMapping = std::map<std::string, size_t> {
    {"B", 1},
    {"KB", cKilobyte},
    {"MB", cKilobyte* cKilobyte},
    {"GB", cKilobyte* cKilobyte* cKilobyte},
    {"TB", cKilobyte* cKilobyte* cKilobyte* cKilobyte},
};

RetWithError<std::string> PathToDevice(const String& path)
{
    constexpr auto cMajorMinorIndex  = 2;
    constexpr auto cMountSourceIndex = 9;

    struct stat pathStat;

    if (stat(path.CStr(), &pathStat) != 0) {
        return {"", AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "failed to get device ID"))};
    }
    const auto majorMinor
        = std::to_string(major(pathStat.st_dev)).append(":").append(std::to_string(minor(pathStat.st_dev)));

    std::ifstream file("/proc/self/mountinfo");
    if (!file.is_open()) {
        return {"", AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "failed to read mountinfo"))};
    }

    std::string line;

    while (std::getline(file, line)) {
        std::istringstream lineStream(line);

        Poco::StringTokenizer tokenizer(
            line, " ", Poco::StringTokenizer::TOK_IGNORE_EMPTY | Poco::StringTokenizer::TOK_TRIM);

        if (tokenizer.count() <= cMountSourceIndex || tokenizer[cMajorMinorIndex] != majorMinor) {
            continue;
        }

        return {tokenizer[cMountSourceIndex]};
    }

    return {"", ErrorEnum::eNotFound};
}

bool QuotasSupported(const std::string& path)
{
    dqblk quota {};

    if (auto res = quotactl(QCMD(Q_GETQUOTA, USRQUOTA), path.c_str(), 0, reinterpret_cast<char*>(&quota)); res == -1) {
        return false;
    }

    return true;
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

ResourceUsageProvider::ResourceUsageProvider()
    : mCPUCount(std::thread::hardware_concurrency())
{
}

Error ResourceUsageProvider::Init(sm::networkmanager::NetworkManagerItf& networkManager)
{
    LOG_DBG() << "Initialize resource usage provider";

    mNetworkManager = &networkManager;

    return ErrorEnum::eNone;
}

Error ResourceUsageProvider::GetNodeMonitoringData(
    const Array<PartitionInfo>& partitionInfos, MonitoringData& monitoringData)
{
    LOG_DBG() << "Get node monitoring data";

    Error err = ErrorEnum::eNone;

    if (Tie(monitoringData.mCPU, err) = GetSystemCPUUsage(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (Tie(monitoringData.mRAM, err) = GetSystemRAMUsage(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    LOG_DBG() << "Get node monitoring data: CPU(%)=" << monitoringData.mCPU
              << ", RAM(K)=" << (monitoringData.mRAM / cKilobyte);

    for (const auto& partition : partitionInfos) {
        err = monitoringData.mPartitions.EmplaceBack();
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (Tie(monitoringData.mPartitions.Back().mUsedSize, err) = GetSystemDiskUsage(partition.mPath);
            !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        LOG_DBG() << "Get node monitoring data: partition=" << partition.mName
                  << ", used size(K)= " << monitoringData.mPartitions.Back().mUsedSize / cKilobyte;
    }

    if (mNetworkManager) {
        if (err = mNetworkManager->GetSystemTraffic(monitoringData.mDownload, monitoringData.mUpload); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        LOG_DBG() << "Get node monitoring data: download(K)=" << monitoringData.mDownload / cKilobyte
                  << ", upload(K)=" << monitoringData.mUpload / cKilobyte;
    }

    return ErrorEnum::eNone;
}

Error ResourceUsageProvider::GetInstanceMonitoringData(
    const String& instanceID, aos::monitoring::InstanceMonitoringData& monitoringData)
{
    LOG_DBG() << "Get instance monitoring data: instanceID=" << instanceID;

    if (auto err = SetInstanceMonitoringData(instanceID, monitoringData.mMonitoringData); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    LOG_DBG() << "Get instance monitoring data: id=" << instanceID << ", CPU(%)=" << monitoringData.mMonitoringData.mCPU
              << ", RAM(K)=" << (monitoringData.mMonitoringData.mRAM / cKilobyte);

    for (const auto& partition : monitoringData.mPartitions) {
        auto err = monitoringData.mMonitoringData.mPartitions.EmplaceBack();
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        auto& partitionUsage = monitoringData.mMonitoringData.mPartitions.Back();

        err = partitionUsage.mName.Assign(partition.mName);
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (Tie(partitionUsage.mUsedSize, err) = GetInstanceDiskUsage(partition.mPath, monitoringData.mUID);
            !err.IsNone() && !err.Is(ErrorEnum::eNotSupported)) {

            return AOS_ERROR_WRAP(err);
        }

        LOG_DBG() << "Get instance monitoring data: id=" << instanceID << ", partition=" << partition.mName
                  << ", used size(K)= " << partitionUsage.mUsedSize / cKilobyte;
    }

    if (mNetworkManager) {
        if (auto err = mNetworkManager->GetInstanceTraffic(
                instanceID, monitoringData.mMonitoringData.mDownload, monitoringData.mMonitoringData.mUpload);
            !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        LOG_DBG() << "Get instance monitoring data: id=" << instanceID
                  << ", download(K)=" << monitoringData.mMonitoringData.mDownload / cKilobyte
                  << ", upload(K)=" << monitoringData.mMonitoringData.mUpload / cKilobyte;
    }

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

RetWithError<double> ResourceUsageProvider::GetSystemCPUUsage()
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

RetWithError<size_t> ResourceUsageProvider::GetSystemRAMUsage()
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

RetWithError<uint64_t> ResourceUsageProvider::GetSystemDiskUsage(const String& path)
{
    struct statvfs sbuf;

    if (auto ret = statvfs(path.CStr(), &sbuf); ret != 0) {
        return {0, AOS_ERROR_WRAP(Error(ret, "failed to get disk usage"))};
    }

    return {static_cast<uint64_t>(sbuf.f_blocks - sbuf.f_bfree) * static_cast<uint64_t>(sbuf.f_frsize)};
}

RetWithError<size_t> ResourceUsageProvider::GetInstanceCPUUsage(const String& instanceID)
{
    const auto cpuUsageFile = fs::JoinPath(cCgroupsPath, instanceID, cCpuUsageFile);

    std::ifstream file(cpuUsageFile.CStr());
    if (!file.is_open()) {
        return {0, AOS_ERROR_WRAP(ErrorEnum::eNotFound)};
    }

    std::string line;

    while (getline(file, line)) {
        std::istringstream lineStream(line);

        std::string key;
        size_t      value = 0;

        if (lineStream >> key >> value) {
            if (key == "usage_usec") {
                return {value, ErrorEnum::eNone};
            }
        }
    }

    return {0, ErrorEnum::eNotFound};
}

RetWithError<size_t> ResourceUsageProvider::GetInstanceRAMUsage(const String& instanceID)
{
    const auto memUsageFile = fs::JoinPath(cCgroupsPath, instanceID, cMemUsageFile);

    std::ifstream file(memUsageFile.CStr());
    if (!file.is_open()) {
        return {0, AOS_ERROR_WRAP(ErrorEnum::eNotFound)};
    }

    std::string line;

    if (!getline(file, line)) {
        return {0, AOS_ERROR_WRAP(ErrorEnum::eFailed)};
    }

    try {
        return {std::stoull(line), ErrorEnum::eNone};
    } catch (const std::exception& e) {
        return {0, AOS_ERROR_WRAP(common::utils::ToAosError(e))};
    }

    return {0, ErrorEnum::eNotFound};
}

RetWithError<uint64_t> ResourceUsageProvider::GetInstanceDiskUsage(const String& path, uint32_t uid)
{
    auto [devicePath, err] = PathToDevice(path);
    if (!err.IsNone()) {
        return {0, AOS_ERROR_WRAP(Error(err, "failed to get mount device"))};
    }

    if (!QuotasSupported(devicePath)) {
        return {0, ErrorEnum::eNotSupported};
    }

    dqblk quota {};

    if (auto res = quotactl(QCMD(Q_GETQUOTA, USRQUOTA), devicePath.c_str(), uid, reinterpret_cast<char*>(&quota));
        res == -1) {
        return {0, ErrorEnum::eFailed};
    }

    return static_cast<uint64_t>(quota.dqb_curspace);
}

Error ResourceUsageProvider::SetInstanceMonitoringData(const String& instanceID, MonitoringData& monitoringData)
{
    auto it = mInstanceMonitoringCache.find(instanceID.CStr());
    if (it == mInstanceMonitoringCache.end()) {
        it = mInstanceMonitoringCache.emplace(instanceID.CStr(), CPUUsage()).first;
    }

    auto& cachedInstanceMonitoring = it->second;

    Error  err      = ErrorEnum::eNone;
    size_t cpuUsage = 0;

    if (Tie(monitoringData.mRAM, err) = GetInstanceRAMUsage(instanceID); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (Tie(cpuUsage, err) = GetInstanceCPUUsage(instanceID); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (cachedInstanceMonitoring.mTotal > cpuUsage) {
        cachedInstanceMonitoring.mTotal = 0;
    }

    const auto now                   = Time::Now();
    const auto timeDeltaMicroseconds = static_cast<double>(now.Sub(cachedInstanceMonitoring.mTimestamp).Microseconds());

    if (timeDeltaMicroseconds > 0 && mCPUCount > 0) {
        monitoringData.mCPU = static_cast<double>(cpuUsage - cachedInstanceMonitoring.mTotal) * 100.0
            / timeDeltaMicroseconds / static_cast<double>(mCPUCount);
    }

    cachedInstanceMonitoring.mTotal     = cpuUsage;
    cachedInstanceMonitoring.mTimestamp = now;

    return ErrorEnum::eNone;
}

}; // namespace aos::sm::monitoring
