/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <fstream>
#include <sys/statfs.h>
#include <unordered_map>
#include <vector>

#include <utils/exception.hpp>
#include <utils/parser.hpp>

#include "logger/logmodule.hpp"
#include "systeminfo.hpp"

/***********************************************************************************************************************
 * Constants
 **********************************************************************************************************************/

const uint64_t cBytesPerKB = 1024;

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

namespace {

class CPUInfoParser {
public:
    aos::Error GetCPUInfo(const std::string& path, aos::Array<aos::CPUInfo>& cpuInfoArray)
    {
        if (mFile.open(path); !mFile.is_open()) {
            return aos::ErrorEnum::eNotFound;
        }

        if (const auto err = ParseCPUInfoFile(); !err.IsNone()) {
            return err;
        }

        for (const auto& item : mCPUInfos) {
            if (const auto err = cpuInfoArray.PushBack(item.second); !err.IsNone()) {
                return err;
            }
        }

        return aos::ErrorEnum::eNone;
    }

private:
    void PopulateCPUInfoObject()
    {
        if (mCurrentEntryKeyValues.empty()) {
            return;
        }

        size_t       physicalId = 0;
        aos::CPUInfo cpuInfo;

        for (const auto& keyValue : mCurrentEntryKeyValues) {
            try {
                if (keyValue.mKey == "physical id") {
                    physicalId = std::stoul(keyValue.mValue);
                } else if (keyValue.mKey == "model name") {
                    cpuInfo.mModelName = keyValue.mValue.c_str();
                } else if (keyValue.mKey == "cpu cores") {
                    cpuInfo.mNumCores = std::stoul(keyValue.mValue);
                } else if (keyValue.mKey == "siblings") {
                    cpuInfo.mNumThreads = std::stoul(keyValue.mValue);
                } else if (keyValue.mKey == "cpu family") {
                    cpuInfo.mArch = keyValue.mValue.c_str();
                }
            } catch (...) {
                LOG_DBG() << "CPU info parsing failed: key=" << keyValue.mKey.c_str()
                          << ", value=" << keyValue.mValue.c_str();

                throw aos::common::utils::AosException("Failed to parse CPU info", aos::ErrorEnum::eFailed);
            }
        }

        // only the first entry for the CPU is stored in the map.
        mCPUInfos.insert({physicalId, cpuInfo});

        mCurrentEntryKeyValues.clear();
    }

    aos::Error ParseCPUInfoFile() noexcept
    {
        std::string line;

        try {

            while (std::getline(mFile, line)) {
                const auto keyValue = aos::common::utils::ParseKeyValue(line);

                if (!keyValue.has_value() || keyValue->mKey == "processor") {
                    PopulateCPUInfoObject();
                }

                if (keyValue.has_value()) {
                    mCurrentEntryKeyValues.push_back(std::move(keyValue.value()));
                }
            }

            // populate last CPU info object
            PopulateCPUInfoObject();
        } catch (...) {
            LOG_ERR() << "Failed to parse CPU info file: line=" << line.c_str();

            return aos::ErrorEnum::eFailed;
        }

        return aos::ErrorEnum::eNone;
    }

    std::ifstream                             mFile;
    std::unordered_map<size_t, aos::CPUInfo>  mCPUInfos;
    std::vector<aos::common::utils::KeyValue> mCurrentEntryKeyValues;
};

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

namespace UtilsSystemInfo {

aos::Error GetCPUInfo(const std::string& path, aos::Array<aos::CPUInfo>& cpuInfoArray) noexcept
{
    try {
        CPUInfoParser parser;

        return parser.GetCPUInfo(path, cpuInfoArray);
    } catch (const std::exception& e) {
        return aos::Error(aos::ErrorEnum::eFailed, e.what());
    }
}

aos::RetWithError<uint64_t> GetMemTotal(const std::string& path) noexcept
{
    try {
        std::ifstream file;

        if (file.open(path); !file.is_open()) {
            return {0, aos::ErrorEnum::eNotFound};
        }

        std::string line;

        while (std::getline(file, line)) {
            const auto keyValue = aos::common::utils::ParseKeyValue(line);

            if (!keyValue.has_value() || keyValue->mKey != "MemTotal") {
                continue;
            }

            const auto memTotalKB = std::stoull(keyValue->mValue.substr(0, keyValue->mValue.find(" ")));

            // convert KB to bytes
            return {memTotalKB * cBytesPerKB, aos::ErrorEnum::eNone};
        }

    } catch (...) {
        return {0, aos::ErrorEnum::eRuntime};
    }

    return {0, aos::ErrorEnum::eFailed};
}

aos::RetWithError<uint64_t> GetMountFSTotalSize(const std::string& path) noexcept
{
    struct statfs stat { };

    if (statfs(path.c_str(), &stat) == -1) {
        return {0, aos::ErrorEnum::eFailed};
    }

    return {stat.f_blocks * stat.f_bsize, aos::ErrorEnum::eNone};
}

} // namespace UtilsSystemInfo
