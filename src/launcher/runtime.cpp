/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <sys/sysmacros.h>

#include "logger/logmodule.hpp"

#include "runtime.hpp"

namespace aos::sm::launcher {
namespace fs = std::filesystem;

namespace {

/***********************************************************************************************************************
 * consts
 **********************************************************************************************************************/

constexpr auto cDirPermissions = fs::perms::owner_all | fs::perms::group_exec | fs::perms::group_read
    | fs::perms::others_exec | fs::perms::others_read;
constexpr auto cFilePermissions
    = fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read | fs::perms::others_read;

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

unsigned GetMountPermissions(const Mount& mount)
{
    for (const auto& option : mount.mOptions) {
        auto nameValue = std::string(option.CStr());

        auto pos = nameValue.find('=');
        if (pos == std::string::npos) {
            continue;
        }

        if (nameValue.substr(0, pos) != "mode") {
            continue;
        }

        return std::stoul(nameValue.substr(pos, std::string::npos), nullptr, 8);
    }

    return 0;
}

void CreateMountPoint(const fs::path& path, const Mount& mount, bool isDir)
{
    auto mountPoint = (fs::path(path) += mount.mDestination.CStr());

    if (isDir) {
        fs::create_directories(mountPoint);
        fs::permissions(mountPoint, cDirPermissions);
    } else {
        auto dirPath = mountPoint.parent_path();

        fs::create_directories(dirPath);
        fs::permissions(dirPath, cDirPermissions);

        std::ofstream file(mountPoint);
        fs::permissions(mountPoint, cFilePermissions);
    }

    auto permissions = GetMountPermissions(mount);
    if (permissions != 0) {
        fs::permissions(mountPoint, fs::perms(permissions));
    }
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error Runtime::CreateHostFSWhiteouts(const String& path, const Array<StaticString<cFilePathLen>>& hostBinds)
{
    try {
        auto destPath = fs::path(path.CStr());

        fs::create_directories(destPath);
        fs::permissions(destPath, cDirPermissions);

        for (const auto& entry : fs::directory_iterator("/")) {
            if (hostBinds
                    .FindIf([&entry](const StaticString<cFilePathLen>& bind) {
                        return entry.path() == fs::path("/") / bind.CStr();
                    })
                    .mError.IsNone()) {
                continue;
            }

            auto itemPath(fs::path(destPath) += entry.path());

            if (fs::exists(itemPath)) {
                continue;
            }

            LOG_DBG() << "Create rootfs white out: path=" << itemPath.c_str();

            if (auto ret = mknod(itemPath.c_str(), S_IFCHR, makedev(0, 0)); ret != 0) {
                return Error(ErrorEnum::eRuntime, "can't create white out");
            }
        }
    }

    catch (const std::exception& e) {
        return Error(ErrorEnum::eRuntime, e.what());
    }

    return ErrorEnum::eNone;
}

Error Runtime::CreateMountPoints(const String& mountPointDir, const Array<Mount>& mounts)
{
    try {
        for (const auto& mount : mounts) {
            if (mount.mType == "proc" || mount.mType == "tmpfs" || mount.mType == "sysfs") {
                CreateMountPoint(mountPointDir.CStr(), mount, true);
            } else if (mount.mType == "bind") {
                CreateMountPoint(mountPointDir.CStr(), mount, fs::is_directory(mount.mSource.CStr()));
            }
        }
    } catch (const std::exception& e) {
        return Error(ErrorEnum::eRuntime, e.what());
    }

    return ErrorEnum::eNone;
}

} // namespace aos::sm::launcher
