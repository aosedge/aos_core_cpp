/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_UTILS_FSPLATFORM_HPP_
#define AOS_COMMON_UTILS_FSPLATFORM_HPP_

#include <aos/common/tools/fs.hpp>

namespace aos::common::utils {

class FSPlatform : public fs::FSPlatformItf {
public:
    /**
     * Gets mount point for the given directory.
     *
     * @param dir directory path.
     * @return RetWithError<StaticString<cFilePathLen>>.
     */
    RetWithError<StaticString<cFilePathLen>> GetMountPoint(const String& dir) const override;

    /**
     * Gets total size of the file system.
     *
     * @param dir directory path.
     * @return RetWithError<size_t>.
     */
    RetWithError<size_t> GetTotalSize(const String& dir) const override;

    /**
     * Gets directory size.
     *
     * @param dir directory path.
     * @return RetWithError<size_t>.
     */
    RetWithError<size_t> GetDirSize(const String& dir) const override;

    /**
     * Gets available size.
     *
     * @param dir directory path.
     * @return RetWithError<size_t>.
     */
    RetWithError<size_t> GetAvailableSize(const String& dir) const override;

    /**
     * Sets user quota for the given path.
     *
     * @param path path to set quota for.
     * @param quota quota size in bytes.
     * @param uid user ID.
     * @return Error.
     */
    Error SetUserQuota(const String& path, size_t quota, size_t uid) const override;

private:
    static constexpr size_t cStatBlockSize = 512;
    static constexpr auto   cMtabPath      = "/proc/mounts";
};

} // namespace aos::common::utils

#endif
