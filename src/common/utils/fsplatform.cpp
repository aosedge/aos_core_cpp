/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <memory>
#include <sys/quota.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <core/common/tools/fs.hpp>
#include <core/common/tools/logger.hpp>

#include "exception.hpp"
#include "filesystem.hpp"
#include "fsplatform.hpp"

namespace aos::common::utils {

RetWithError<StaticString<cFilePathLen>> FSPlatform::GetMountPoint(const String& dir) const
{
    auto [mountPoint, err] = common::utils::GetMountPoint(dir.CStr());
    if (!err.IsNone()) {
        return {"", AOS_ERROR_WRAP(err)};
    }

    return {mountPoint.c_str(), ErrorEnum::eNone};
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

Error FSPlatform::SetUserQuota(const String& path, uid_t uid, size_t quota) const
{
    if (quota == 0) {
        return ErrorEnum::eNone;
    }

    auto [device, err] = GetBlockDevice(path.CStr());
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    LOG_DBG() << "Set quota" << Log::Field("path", path) << Log::Field("device", device) << Log::Field("quota", quota)
              << Log::Field("uid", uid);

    dqblk dq {};

    dq.dqb_bhardlimit = (quota + 1023) / 1024;
    dq.dqb_valid      = QIF_BLIMITS;

    if (auto res
        = quotactl(QCMD(Q_SETQUOTA, USRQUOTA), device.CStr(), static_cast<int>(uid), reinterpret_cast<char*>(&dq));
        res == -1) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, strerror(errno)));
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

RetWithError<StaticString<cDeviceNameLen>> FSPlatform::GetBlockDevice(const String& path) const
{
    auto [device, err] = common::utils::GetBlockDevice(path.CStr());
    if (!err.IsNone()) {
        return {"", AOS_ERROR_WRAP(err)};
    }

    return {device.c_str(), ErrorEnum::eNone};
}

} // namespace aos::common::utils
