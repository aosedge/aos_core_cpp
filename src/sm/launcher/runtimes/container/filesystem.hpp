/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_CONTAINER_FILESYSTEM_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_CONTAINER_FILESYSTEM_HPP_

#include <string>
#include <vector>

#include "itf/filesystem.hpp"

namespace aos::sm::launcher {

class FileSystem : public FileSystemItf {
public:
    /**
     * Creates host FS whiteouts.
     *
     * @param path path to whiteouts.
     * @param hostBinds host binds.
     * @return Error.
     */
    Error CreateHostFSWhiteouts(const std::string& path, const std::vector<std::string>& hostBinds) override;

    /**
     * Creates mount points.
     *
     * @param mountPointDir mount point directory.
     * @param mounts mounts to create.
     * @return Error.
     */
    Error CreateMountPoints(const std::string& mountPointDir, const std::vector<Mount>& mounts) override;

    /**
     * Mounts root FS for Aos service.
     *
     * @param rootfsPath path to service root FS.
     * @param layers layers to mount.
     * @return Error.
     */
    Error MountServiceRootFS(const std::string& rootfsPath, const std::vector<std::string>& layers) override;

    /**
     * Umounts Aos service root FS.
     *
     * @param rootfsPath path to service root FS.
     * @return Error.
     */
    Error UmountServiceRootFS(const std::string& rootfsPath) override;

    /**
     * Prepares Aos service storage directory.
     *
     * @param path service storage directory.
     * @param uid user ID.
     * @param gid group ID.
     * @return Error.
     */
    Error PrepareServiceStorage(const std::string& path, uid_t uid, gid_t gid) override;

    /**
     * Prepares Aos service state file.
     *
     * @param path service state file path.
     * @param uid user ID.
     * @param gid group ID.
     * @return Error.
     */
    Error PrepareServiceState(const std::string& path, uid_t uid, gid_t gid) override;

    /**
     * Prepares directory for network files.
     *
     * @param path network directory path.
     * @return Error.
     */
    Error PrepareNetworkDir(const std::string& path) override;

    /**
     * Returns absolute path of FS item.
     *
     * @param path path to convert.
     * @return RetWithError<std::string>.
     */
    RetWithError<std::string> GetAbsPath(const std::string& path) override;

    /**
     * Returns GID by group name.
     *
     * @param groupName group name.
     * @return RetWithError<gid_t>.
     */
    RetWithError<gid_t> GetGIDByName(const std::string& groupName) override;

    /**
     * Populates host devices.
     *
     * @param devicePath device path.
     * @param[out] devices OCI devices.
     * @return Error.
     */
    Error PopulateHostDevices(const std::string& devicePath, std::vector<oci::LinuxDevice>& devices) override;

    /**
     * Clears directory.
     *
     * @param path directory path.
     * @return Error.
     */
    Error ClearDir(const std::string& path) override;

    /**
     * Creates directory and all parent directories if not exist.
     *
     * @param path directory path.
     * @return Error.
     */
    Error MakeDirAll(const std::string& path) override;

    /**
     * Removes all files and directories at path.
     *
     * @param path path to remove.
     * @return Error.
     */
    Error RemoveAll(const std::string& path) override;

    /**
     * Lists directory contents (only directories).
     *
     * @param path directory path.
     * @return RetWithError<std::vector<std::string>>.
     */
    RetWithError<std::vector<std::string>> ListDir(const std::string& path) override;
};

} // namespace aos::sm::launcher

#endif
