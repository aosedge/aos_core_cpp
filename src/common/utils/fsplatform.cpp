/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <filesystem>
#include <memory>
#include <mntent.h>
#include <sys/quota.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <core/common/tools/fs.hpp>

#include "exception.hpp"
#include "filesystem.hpp"
#include "fsplatform.hpp"

namespace aos::common::utils {

RetWithError<StaticString<cFilePathLen>> FSPlatform::GetMountPoint(const String& dir) const
{
    struct stat dirStat;

    if (stat(dir.CStr(), &dirStat) != 0) {
        return {0, Error(ErrorEnum::eNotFound, "failed to stat directory")};
    }

    auto mtab = std::unique_ptr<FILE, decltype(&endmntent)>(setmntent(cMtabPath, "r"), endmntent);
    if (!mtab) {
        return {0, Error(ErrorEnum::eNotFound, "failed to open /proc/mounts")};
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
        return {0, Error(ErrorEnum::eNotFound, "failed to find mount point")};
    }

    return {bestMountPoint.c_str(), ErrorEnum::eNone};
}

RetWithError<size_t> FSPlatform::GetTotalSize(const String& dir) const
{
    struct statvfs st;

    if (statvfs(dir.CStr(), &st) != 0) {
        return {0, Error(ErrorEnum::eNotFound, "failed to get total size")};
    }

    return size_t(st.f_blocks) * st.f_frsize;
}

RetWithError<size_t> FSPlatform::GetDirSize(const String& dir) const
{
    return fs::CalculateSize(dir);
}

RetWithError<size_t> FSPlatform::GetAvailableSize(const String& dir) const
{
    struct statvfs st;

    if (statvfs(dir.CStr(), &st) != 0) {
        return {0, Error(ErrorEnum::eNotFound, "failed to get available size")};
    }

    return size_t(st.f_bavail) * st.f_frsize;
}

Error FSPlatform::SetUserQuota(const String& path, size_t quota, size_t uid) const
{
    dqblk dq {};

    dq.dqb_bhardlimit = quota;
    dq.dqb_valid      = QIF_BLIMITS;

    if (auto res
        = quotactl(QCMD(Q_SETQUOTA, USRQUOTA), path.CStr(), static_cast<int>(uid), reinterpret_cast<char*>(&dq));
        res == -1) {
        return Error(errno);
    }

    return ErrorEnum::eNone;
}

Error FSPlatform::ChangeOwner(const String& path, uint32_t uid, uint32_t gid) const
{
    try {
        common::utils::ChangeOwner(path.CStr(), uid, gid);
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

} // namespace aos::common::utils
