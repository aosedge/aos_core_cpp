/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <memory>
#include <string>

#include <fcntl.h>
#include <sched.h>
#include <sys/mount.h>
#include <unistd.h>

#include <aos/common/tools/memory.hpp>

#include "logger/logmodule.hpp"
#include "network/namespacemanager.hpp"

namespace aos::common::network {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error NamespaceManager::Init(sm::networkmanager::NetworkInterfaceManagerItf& netIf)
{
    LOG_DBG() << "Init namespace manager";

    mNetIf = &netIf;

    std::filesystem::create_directory(cPathToNetNs);

    return ErrorEnum::eNone;
}

Error NamespaceManager::CreateNetworkNamespace(const String& ns)
{
    LOG_DBG() << "Create network namespace: ns=" << ns;

    auto path = std::filesystem::path(cPathToNetNs) / ns.CStr();

    if (std::filesystem::exists(path)) {
        return ErrorEnum::eNone;
    }

    Error                err;
    StaticString<cNSLen> originalNsPath;

    if (err = originalNsPath.Format(cNSPathFormat, getpid(), gettid()); !err.IsNone()) {
        return err;
    }

    int originalNsFd = open(originalNsPath.CStr(), O_RDONLY | O_CLOEXEC);
    if (originalNsFd < 0) {
        return Error(ErrorEnum::eFailed, strerror(errno));
    }

    auto closeFd         = [](int* fd) { close(*fd); };
    auto originalNsFdPtr = std::unique_ptr<int, decltype(closeFd)>(&originalNsFd, closeFd);

    // Unshare to create the new network namespace
    if (unshare(CLONE_NEWNET) != 0) {
        return Error(ErrorEnum::eFailed, strerror(errno));
    }

    int nsFd = open(path.c_str(), O_CREAT | O_EXCL | O_RDWR, 0444);
    if (nsFd < 0) {
        return Error(ErrorEnum::eFailed, strerror(errno));
    }

    close(nsFd);

    auto autoSwitchToOriginNs = DeferRelease(&originalNsFd, [&path, &err](int* fd) {
        if (setns(*fd, CLONE_NEWNET) != 0) {
            LOG_ERR() << "Failed to return to original namespace: " << strerror(errno);

            if (unlink(path.c_str()) != 0) {
                LOG_ERR() << "Failed to remove namespace file: err=" << strerror(errno);
            }

            return;
        }

        if (!err.IsNone()) {
            LOG_ERR() << "Error in creating network namespace, err=" << err;

            if (unlink(path.c_str()) != 0) {
                LOG_ERR() << "Failed to remove namespace file: err=" << strerror(errno);
            }
        }
    });

    StaticString<cNSLen> currentNsPath;

    if (err = currentNsPath.Format(cNSPathFormat, getpid(), gettid()); !err.IsNone()) {
        return err;
    }

    if (mount(currentNsPath.CStr(), path.c_str(), "none", MS_BIND, nullptr) != 0) {
        return Error(ErrorEnum::eFailed, strerror(errno));
    }

    if (err = mNetIf->BringUpInterface("lo"); !err.IsNone()) {
        return err;
    }

    return ErrorEnum::eNone;
}

RetWithError<StaticString<cFilePathLen>> NamespaceManager::GetNetworkNamespacePath(const String& ns) const
{
    LOG_DBG() << "Get network namespace path: ns=" << ns;

    auto path = std::filesystem::path(cPathToNetNs) / ns.CStr();

    return {path.c_str(), ErrorEnum::eNone};
}

Error NamespaceManager::DeleteNetworkNamespace(const String& ns)
{
    LOG_DBG() << "Delete network namespace: ns=" << ns;

    auto path = std::filesystem::path(cPathToNetNs) / ns.CStr();

    if (!std::filesystem::exists(path)) {
        return ErrorEnum::eNone;
    }

    if (umount2(path.c_str(), MNT_DETACH) != 0) {
        LOG_ERR() << "Failed to unmount namespace: err=" << strerror(errno);

        return Error(ErrorEnum::eFailed, "failed to unmount namespace");
    }

    if (unlink(path.c_str()) != 0) {
        LOG_ERR() << "Failed to remove namespace file: err=" << strerror(errno);

        return Error(ErrorEnum::eFailed, "failed to remove namespace file");
    }

    return ErrorEnum::eNone;
}

} // namespace aos::common::network
