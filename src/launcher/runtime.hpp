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
     * Prepares root FS for Aos service.
     *
     * @param rootfsPath path to service root FS.
     * @param mountPointDir mount point directory.
     * @param mounts mounts to prepare.
     * @param layers layers to prepare.
     * @return Error.
     */
    Error PrepareServiceRootFS(const String& rootfsPath, const String& mountPointDir, const Array<Mount>& mounts,
        const Array<StaticString<cFilePathLen>>& layers) override
    {
        (void)rootfsPath;
        (void)mountPointDir;
        (void)mounts;
        (void)layers;

        return ErrorEnum::eNone;
    }

    /**
     * Releases Aos service root FS.
     *
     * @param runtimeDir service runtime directory.
     * @return Error.
     */
    Error ReleaseServiceRootFS(const String& runtimeDir) override
    {
        (void)runtimeDir;

        return ErrorEnum::eNone;
    }

    /**
     * Prepares Aos service storage directory.
     *
     * @param path service storage directory.
     * @param uid user ID.
     * @param gid group ID.
     * @return Error.
     */
    Error PrepareServiceStorage(const String& path, uint32_t uid, uint32_t gid) override
    {
        (void)path;
        (void)uid;
        (void)gid;

        return ErrorEnum::eNone;
    }

    /**
     * Prepares Aos service state file.
     *
     * @param path service state file path.
     * @param uid user ID.
     * @param gid group ID.
     * @return Error.
     */
    Error PrepareServiceState(const String& path, uint32_t uid, uint32_t gid) override
    {
        (void)path;
        (void)uid;
        (void)gid;

        return ErrorEnum::eNone;
    }

    /**
     * Returns absolute path of FS item.
     *
     * @param path path to convert.
     * @return RetWithError<StaticString<cFilePathLen>>.
     */
    RetWithError<StaticString<cFilePathLen>> GetAbsPath(const String& path) override
    {
        return {path, ErrorEnum::eNone};
    }

    /**
     * Returns GID by group name.
     *
     * @param groupName group name.
     * @return RetWithError<uint32_t>.
     */
    RetWithError<uint32_t> GetGIDByName(const String& groupName) override
    {
        (void)groupName;

        return {0, ErrorEnum::eNone};
    }

    /**
     * Populates host devices.
     *
     * @param devicePath device path.
     * @param devices OCI devices.
     * @return Error.
     */
    Error PopulateHostDevices(const String& devicePath, const Array<oci::LinuxDevice>& devices)
    {
        (void)devicePath;
        (void)devices;

        return ErrorEnum::eNone;
    }
};

} // namespace aos::sm::launcher

#endif
