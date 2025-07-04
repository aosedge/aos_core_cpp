/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <numeric>
#include <string>
#include <unistd.h>
#include <vector>

#include <Poco/UUID.h>
#include <Poco/UUIDGenerator.h>

#include "exception.hpp"
#include "filesystem.hpp"

namespace fs = std::filesystem;

namespace aos::common::utils {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

RetWithError<std::string> MkTmpDir(const std::string& dir, const std::string& pattern)
{
    std::string directory   = dir.empty() ? fs::temp_directory_path().string() : dir;
    std::string tempPattern = pattern.empty() ? "tmp.XXXXXX" : pattern;

    if (tempPattern.length() < 7 || tempPattern.substr(tempPattern.length() - 7) != ".XXXXXX") {
        tempPattern += ".XXXXXX";
    }

    std::string fullPath = (fs::path(directory) / tempPattern).string();

    std::vector<char> mutablePath(fullPath.begin(), fullPath.end());
    mutablePath.push_back('\0');

    char* result = mkdtemp(mutablePath.data());

    if (result == nullptr) {
        return {"", Error(ErrorEnum::eFailed, strerror(errno))};
    }

    return {std::string(result), ErrorEnum::eNone};
}

RetWithError<uintmax_t> CalculateSize(const std::string& path)
{
    if (fs::is_regular_file(path)) {
        return fs::file_size(path);
    }

    if (fs::is_directory(path)) {
        return std::accumulate(fs::recursive_directory_iterator(path), fs::recursive_directory_iterator(), 0,
            [](uintmax_t total, const auto& entry) {
                return (fs::is_regular_file(entry)) ? (total + fs::file_size(entry)) : total;
            });
    }

    return {0, ErrorEnum::eNotSupported};
}

Error ChangeOwner(const std::string& path, uint32_t newUID, uint32_t newGID)
{
    try {
        auto changeOwner = [](const std::string& filePath, uint32_t uid, uint32_t gid) {
            if (chown(filePath.c_str(), uid, gid) == -1) {
                AOS_ERROR_THROW(errno, "can't change file owner");
            }
        };

        changeOwner(path, newUID, newGID);

        if (fs::is_regular_file(path)) {
            return ErrorEnum::eNone;
        }

        for (const auto& entry : fs::recursive_directory_iterator(path)) {
            const std::string filePath = entry.path().string();

            changeOwner(filePath, newUID, newGID);
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

} // namespace aos::common::utils
