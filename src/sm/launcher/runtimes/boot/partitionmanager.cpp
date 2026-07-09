/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <blkid/blkid.h>
#include <common/utils/retry.hpp>
#include <filesystem>
#include <fstream>
#include <sys/mount.h>

#include <core/common/tools/logger.hpp>

#include <common/utils/exception.hpp>
#include <common/utils/utils.hpp>

#include "partitionmanager.hpp"

namespace aos::sm::launcher {

namespace {

/***********************************************************************************************************************
 * Consts
 **********************************************************************************************************************/

constexpr auto cTagTypeLabel    = "LABEL";
constexpr auto cTagTypeFSType   = "TYPE";
constexpr auto cTagTypePartUUID = "PARTUUID";
constexpr auto cUmountRetries   = 3;
constexpr auto cUmountDelay     = Time::cSeconds;
constexpr auto cUmountMaxDelay  = 5 * Time::cSeconds;

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

RetWithError<size_t> GetPartitionNumber(const std::string& block)
{
    std::ifstream f("/sys/class/block/" + block + "/partition");
    if (!f) {
        return {{}, ErrorEnum::eNotFound};
    }

    size_t part = 0;

    f >> part;

    return part;
}

std::string GetParentDevice(const std::string& block)
{
    std::filesystem::path p = std::filesystem::canonical("/sys/class/block/" + block);

    return (std::filesystem::path("/dev") / p.parent_path().filename()).string();
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error PartitionManager::GetPartInfo(const std::string& partDevice, PartInfo& partInfo) const
{
    blkid_cache blkcache = nullptr;

    // Initialize blkid cache
    if (blkid_get_cache(&blkcache, "/dev/null") != 0) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, strerror(errno)));
    }

    auto putCache = DeferRelease(blkcache, [](const blkid_cache cache) { blkid_put_cache(cache); });

    auto blkdev = blkid_get_dev(blkcache, partDevice.c_str(), BLKID_DEV_NORMAL);
    if (!blkdev) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, strerror(errno)));
    }

    if (const char* devname = blkid_dev_devname(blkdev); devname) {
        partInfo.mDevice = devname;

        const auto filename = std::filesystem::path(devname).filename().string();

        auto [partNum, err] = GetPartitionNumber(filename);
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        partInfo.mPartitionNumber = partNum;
        partInfo.mParentDevice    = GetParentDevice(filename);
    }

    // Iterate over tags
    blkid_tag_iterate iter = blkid_tag_iterate_begin(blkdev);

    const char* tagType  = nullptr;
    const char* tagValue = nullptr;

    while (blkid_tag_next(iter, &tagType, &tagValue) == 0) {
        if (!tagType || !tagValue)
            continue;

        if (std::string(tagType) == cTagTypeLabel) {
            partInfo.mLabel = tagValue;
        } else if (std::string(tagType) == cTagTypeFSType) {
            partInfo.mFSType = tagValue;
        } else if (std::string(tagType) == cTagTypePartUUID) {
            partInfo.mPartUUID = tagValue;
        }
    }

    blkid_tag_iterate_end(iter);

    return ErrorEnum::eNone;
}

Error PartitionManager::Mount(const PartInfo& partInfo, const std::string& mountPoint, int flags) const
{
    if (mount(partInfo.mDevice.c_str(), mountPoint.c_str(), partInfo.mFSType.c_str(), flags, nullptr) != 0) {
        return Error(ErrorEnum::eFailed, strerror(errno));
    }

    return ErrorEnum::eNone;
}

Error PartitionManager::Unmount(const std::string& mountPoint) const
{
    auto err = common::utils::Retry(
        [mountPoint]() {
            if (umount2(mountPoint.c_str(), 0) != 0) {
                return ErrorEnum::eFailed;
            }

            return ErrorEnum::eNone;
        },
        {}, cUmountRetries, cUmountDelay, cUmountMaxDelay);

    if (!err.IsNone()) {
        if (umount2(mountPoint.c_str(), MNT_FORCE) != 0) {
            return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, strerror(errno)));
        }
    }

    return ErrorEnum::eNone;
}

Error PartitionManager::CopyDevice(const std::string& src, const std::string& dst) const
{
    if (src == dst) {
        return ErrorEnum::eNone;
    }

    auto result = common::utils::ExecCommand({"dd", "if=" + src, "of=" + dst, "bs=1M"});
    if (!result.mError.IsNone()) {
        return AOS_ERROR_WRAP(result.mError);
    }

    return ErrorEnum::eNone;
}

Error PartitionManager::InstallImage(const std::string& image, const std::string& device) const
{
    auto result = common::utils::ExecCommand({"dd", "if=" + image, "of=" + device, "bs=1M"});
    if (!result.mError.IsNone()) {
        return AOS_ERROR_WRAP(result.mError);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::sm::launcher
