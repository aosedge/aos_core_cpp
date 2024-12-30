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
    Error CreateHostFSWhiteouts(const String& path, const Array<StaticString<cFilePathLen>>& hostBinds) override
    {
        (void)path;
        (void)hostBinds;

        return ErrorEnum::eNone;
    }

    /**
     * Prepares root FS for Aos service.
     *
     * @param rootfsPath path to service root FS.
     * @param mountPointDir mount point directory.
     * @param mounts mounts to prepare.
     * @param layers layers to prepare.
     * @return Error.
     */
    Error PrepareServiceRootFS(const String& rootfsPath, const String& mountPointDir, const Array<oci::Mount>& mounts,
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
};

} // namespace aos::sm::launcher

#endif
