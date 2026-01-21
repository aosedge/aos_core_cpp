/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_CONTAINER_ITF_FILESYSTEM_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_CONTAINER_ITF_FILESYSTEM_HPP_

#include <string>
#include <vector>

#include <core/common/ocispec/itf/ocispec.hpp>

namespace aos::sm::launcher {

/**
 * File system interface.
 */
class FileSystemItf {
public:
    /**
     * Destructor.
     */
    virtual ~FileSystemItf() = default;

    /**
     * Creates host FS whiteouts.
     *
     * @param path path to whiteouts.
     * @param hostBinds host binds.
     * @return Error.
     */
    virtual Error CreateHostFSWhiteouts(const std::string& path, const std::vector<std::string>& hostBinds) = 0;

    /**
     * Creates mount points.
     *
     * @param mountPointDir mount point directory.
     * @param mounts mounts to create.
     * @return Error.
     */
    virtual Error CreateMountPoints(const std::string& mountPointDir, const std::vector<Mount>& mounts) = 0;

    /**
     * Mounts root FS for Aos service.
     *
     * @param rootfsPath path to service root FS.
     * @param layers layers to mount.
     * @return Error.
     */
    virtual Error MountServiceRootFS(const std::string& rootfsPath, const std::vector<std::string>& layers) = 0;

    /**
     * Umounts Aos service root FS.
     *
     * @param rootfsPath path to service root FS.
     * @return Error.
     */
    virtual Error UmountServiceRootFS(const std::string& rootfsPath) = 0;

    /**
     * Prepares Aos service storage directory.
     *
     * @param path service storage directory.
     * @param uid user ID.
     * @param gid group ID.
     * @return Error.
     */
    virtual Error PrepareServiceStorage(const std::string& path, uid_t uid, gid_t gid) = 0;

    /**
     * Prepares Aos service state file.
     *
     * @param path service state file path.
     * @param uid user ID.
     * @param gid group ID.
     * @return Error.
     */
    virtual Error PrepareServiceState(const std::string& path, uid_t uid, gid_t gid) = 0;

    /**
     * Prepares directory for network files.
     *
     * @param path network directory path.
     * @return Error.
     */
    virtual Error PrepareNetworkDir(const std::string& path) = 0;

    /**
     * Returns absolute path of FS item.
     *
     * @param path path to convert.
     * @return RetWithError<std::string>.
     */
    virtual RetWithError<std::string> GetAbsPath(const std::string& path) = 0;

    /**
     * Returns GID by group name.
     *
     * @param groupName group name.
     * @return RetWithError<gid_t>.
     */
    virtual RetWithError<gid_t> GetGIDByName(const std::string& groupName) = 0;

    /**
     * Populates host devices.
     *
     * @param devicePath device path.
     * @param[out] devices OCI devices.
     */
    virtual Error PopulateHostDevices(const std::string& devicePath, std::vector<oci::LinuxDevice>& devices) = 0;

    /**
     * Creates directory and all parent directories if not exist.
     *
     * @param path directory path.
     * @return Error.
     */
    virtual Error MakeDirAll(const std::string& path) = 0;

    /**
     * Clears directory.
     *
     * @param path directory path.
     * @return Error.
     */
    virtual Error ClearDir(const std::string& path) = 0;

    /**
     * Removes all files and directories at path.
     *
     * @param path path to remove.
     * @return Error.
     */
    virtual Error RemoveAll(const std::string& path) = 0;

    /**
     * Lists directory contents (only directories).
     *
     * @param path directory path.
     * @return RetWithError<std::vector<std::string>>.
     */
    virtual RetWithError<std::vector<std::string>> ListDir(const std::string& path) = 0;
};

} // namespace aos::sm::launcher

#endif
