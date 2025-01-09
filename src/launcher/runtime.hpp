/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef RUNTIME_HPP_
#define RUNTIME_HPP_

#include <aos/sm/launcher.hpp>

namespace aos::sm::launcher {

class Runtime : public RuntimeItf {
public:
    /**
     * Creates host FS whiteouts.
     *
     * @param path path to whiteouts.
     * @param hostBinds host binds.
     * @return Error.
     */
    Error CreateHostFSWhiteouts(const String& path, const Array<StaticString<cFilePathLen>>& hostBinds) override;

    /**
     * Creates mount points.
     *
     * @param mountPointDir mount point directory.
     * @param mounts mounts to create.
     * @return Error.
     */
    virtual Error CreateMountPoints(const String& mountPointDir, const Array<Mount>& mounts) override;

    /**
     * Mounts root FS for Aos service.
     *
     * @param rootfsPath path to service root FS.
     * @param layers layers to mount.
     * @return Error.
     */
    virtual Error MountServiceRootFS(
        const String& rootfsPath, const Array<StaticString<cFilePathLen>>& layers) override;

    /**
     * Umounts Aos service root FS.
     *
     * @param rootfsPath path to service root FS.
     * @return Error.
     */
    virtual Error UmountServiceRootFS(const String& rootfsPath) override;

    /**
     * Prepares Aos service storage directory.
     *
     * @param path service storage directory.
     * @param uid user ID.
     * @param gid group ID.
     * @return Error.
     */
    Error PrepareServiceStorage(const String& path, uint32_t uid, uint32_t gid) override;

    /**
     * Prepares Aos service state file.
     *
     * @param path service state file path.
     * @param uid user ID.
     * @param gid group ID.
     * @return Error.
     */
    Error PrepareServiceState(const String& path, uint32_t uid, uint32_t gid) override;

    /**
     * Prepares directory for network files.
     *
     * @param path network directory path.
     * @return Error.
     */
    Error PrepareNetworkDir(const String& path) override;

    /**
     * Returns absolute path of FS item.
     *
     * @param path path to convert.
     * @return RetWithError<StaticString<cFilePathLen>>.
     */
    RetWithError<StaticString<cFilePathLen>> GetAbsPath(const String& path) override;

    /**
     * Returns GID by group name.
     *
     * @param groupName group name.
     * @return RetWithError<uint32_t>.
     */
    RetWithError<uint32_t> GetGIDByName(const String& groupName) override;

    /**
     * Populates host devices.
     *
     * @param devicePath device path.
     * @param[out] devices OCI devices.
     * @return Error.
     */
    virtual Error PopulateHostDevices(const String& devicePath, Array<oci::LinuxDevice>& devices) override;
};

} // namespace aos::sm::launcher

#endif
