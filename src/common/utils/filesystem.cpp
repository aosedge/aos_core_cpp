/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <mntent.h>
#include <numeric>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include <Poco/UUID.h>
#include <Poco/UUIDGenerator.h>

#include "exception.hpp"
#include "filesystem.hpp"

namespace fs = std::filesystem;

namespace aos::common::utils {

namespace {

/***********************************************************************************************************************
 * Consts
 **********************************************************************************************************************/

constexpr auto cMtabPath = "/proc/mounts";

}; // namespace

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

void ChangeOwner(const std::string& path, uid_t uid, gid_t gid)
{
    auto changeOwner = [](const std::string& path, uid_t uid, gid_t gid) {
        if (chown(path.c_str(), uid, gid) == -1) {
            AOS_ERROR_THROW(errno, "can't change file owner");
        }
    };

    changeOwner(path, uid, gid);

    if (std::filesystem::is_regular_file(path)) {
        return;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(path)) {
        changeOwner(entry.path().string(), uid, gid);
    }
}

RetWithError<std::string> GetMountPoint(const std::string& dir)
{
    struct stat dirStat;

    if (stat(dir.c_str(), &dirStat) != 0) {
        return {"", Error(ErrorEnum::eNotFound, "failed to stat directory")};
    }

    auto mtab = std::unique_ptr<FILE, decltype(&endmntent)>(setmntent(cMtabPath, "r"), endmntent);
    if (!mtab) {
        return {"", Error(ErrorEnum::eNotFound, "failed to open /proc/mounts")};
    }

    struct mntent* entry;
    std::string    bestMountPoint;

    while ((entry = getmntent(mtab.get())) != nullptr) {
        struct stat mountStat;

        if (stat(entry->mnt_dir, &mountStat) != 0) {
            continue;
        }

        if (dirStat.st_dev == mountStat.st_dev) {
            const char* mp = entry->mnt_dir;

            if (strlen(mp) > bestMountPoint.length()) {
                bestMountPoint = mp;
            }
        }
    }

    if (bestMountPoint.empty()) {
        return {"", Error(ErrorEnum::eNotFound, "failed to find mount point")};
    }

    return bestMountPoint;
}

RetWithError<std::string> GetBlockDevice(const std::string& path)
{
    struct stat dirStat;

    if (stat(path.c_str(), &dirStat) != 0) {
        return {"", Error(ErrorEnum::eNotFound, "failed to stat directory")};
    }

    auto mtab = std::unique_ptr<FILE, decltype(&endmntent)>(setmntent(cMtabPath, "r"), endmntent);
    if (!mtab) {
        return {"", Error(ErrorEnum::eNotFound, "failed to open /proc/mounts")};
    }

    struct mntent* entry;

    while ((entry = getmntent(mtab.get())) != nullptr) {
        struct stat mountStat;

        if (stat(entry->mnt_dir, &mountStat) != 0) {
            continue;
        }

        if (dirStat.st_dev == mountStat.st_dev) {
            return std::string(entry->mnt_fsname);
        }
    }

    return {"", Error(ErrorEnum::eNotFound, "failed to find block device")};
}

} // namespace aos::common::utils
