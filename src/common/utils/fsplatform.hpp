/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_UTILS_FSPLATFORM_HPP_
#define AOS_COMMON_UTILS_FSPLATFORM_HPP_

#include <core/common/tools/fs.hpp>

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
     * @param uid user ID.
     * @param quota quota size in bytes.
     * @return Error.
     */
    Error SetUserQuota(const String& path, uid_t uid, size_t quota) const override;

    /**
     * Changes the owner of a file or directory.
     *
     * @param path path to the file or directory.
     * @param uid new user ID.
     * @param gid new group ID.
     * @return Error.
     */
    Error ChangeOwner(const String& path, uint32_t uid, uint32_t gid) const override;

    /**
     * Gets block device for the given path.
     *
     * @param path path to file or directory.
     * @return RetWithError<StaticString<cDeviceNameLen>>.
     */
    RetWithError<StaticString<cDeviceNameLen>> GetBlockDevice(const String& path) const override;

private:
    static constexpr size_t cStatBlockSize = 512;
};

} // namespace aos::common::utils

#endif
