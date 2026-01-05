/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>
#include <functional>
#include <grp.h>
#include <iostream>
#include <string>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#include <core/common/tools/logger.hpp>

#include <common/utils/exception.hpp>
#include <common/utils/retry.hpp>

#include "filesystem.hpp"

namespace fs = std::filesystem;

namespace aos::sm::launcher {

namespace {

/***********************************************************************************************************************
 * consts
 **********************************************************************************************************************/

constexpr auto cDirPermissions = fs::perms::owner_all | fs::perms::group_exec | fs::perms::group_read
    | fs::perms::others_exec | fs::perms::others_read;
constexpr auto cFilePermissions
    = fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read | fs::perms::others_read;
constexpr auto cStatePermissions = fs::perms::owner_read | fs::perms::owner_write;

constexpr auto cMountRetryCount = 3;
constexpr auto cMountRetryDelay = Time::cSeconds;

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

        return std::stoul(nameValue.substr(pos + 1, std::string::npos), nullptr, 8);
    }

    return 0;
}

void CreateMountPoint(const fs::path& path, const Mount& mount, bool isDir)
{
    auto mountPoint = fs::path(path) / mount.mDestination.CStr();

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

void MountDir(const fs::path& source, const fs::path& mountPoint, const std::string& fsType, unsigned long flags,
    const std::string& opts)
{
    LOG_DBG() << "Mount dir" << Log::Field("source", source.c_str()) << Log::Field("mountPoint", mountPoint.c_str())
              << Log::Field("type", fsType.c_str());

    auto err = common::utils::Retry(
        [&]() { return mount(source.c_str(), mountPoint.c_str(), fsType.c_str(), flags, opts.c_str()); },
        [&]([[maybe_unused]] int retryCount, [[maybe_unused]] Duration delay, const aos::Error& err) {
            LOG_WRN() << "Mount error, try remount" << Log::Field(err);

            sync();
            umount2(mountPoint.c_str(), MNT_FORCE);
        },
        cMountRetryCount, cMountRetryDelay, Duration(0));
    AOS_ERROR_CHECK_AND_THROW(err, "can't mount dir");
}

void MountOverlay(const fs::path& mountPoint, const std::vector<fs::path>& lowerDirs, const fs::path& workDir,
    const fs::path& upperDir)
{
    auto opts = std::string("lowerdir=");

    for (auto it = lowerDirs.begin(); it != lowerDirs.end(); ++it) {
        opts += *it;

        if (it + 1 != lowerDirs.end()) {
            opts += ":";
        }
    }

    if (!upperDir.empty()) {
        if (workDir.empty()) {
            AOS_ERROR_THROW(ErrorEnum::eRuntime, "working dir path should be set");
        }

        fs::remove_all(workDir);
        fs::create_directories(workDir);
        fs::permissions(workDir, cDirPermissions);

        opts += ",workdir=" + workDir.string();
        opts += ",upperdir=" + upperDir.string();
    }

    MountDir("overlay", mountPoint, "overlay", 0, opts);
}

void UmountDir(const fs::path& mountPoint)
{
    LOG_DBG() << "Umount dir" << Log::Field("mountPoint", mountPoint.c_str());

    auto err = common::utils::Retry(
        [&]() {
            sync();
            return umount(mountPoint.c_str());
        },
        [&]([[maybe_unused]] int retryCount, [[maybe_unused]] Duration delay, const aos::Error& err) {
            LOG_WRN() << "Umount error, retry" << Log::Field(err);

            umount2(mountPoint.c_str(), MNT_FORCE);
        },
        cMountRetryCount, cMountRetryDelay, Duration(0));
    AOS_ERROR_CHECK_AND_THROW(err, "can't umount dir");
}

oci::LinuxDevice DeviceFromPath(const fs::path& path)
{
    auto devPath = path;

    if (fs::is_symlink(path)) {
        auto target = fs::read_symlink(path);
        if (target.is_relative()) {
            devPath = (path.parent_path() / target).lexically_normal();
        } else {
            devPath = target;
        }
    }

    struct stat sb;

    auto ret = lstat(devPath.c_str(), &sb);
    AOS_ERROR_CHECK_AND_THROW(ret, "can't get device stat");

    StaticString<oci::cDeviceTypeLen> type;

    switch (sb.st_mode & S_IFMT) {
    case S_IFBLK:
        type = "b";
        break;

    case S_IFCHR:
        type = "c";
        break;

    case S_IFIFO:
        type = "p";
        break;

    default:
        AOS_ERROR_THROW(ErrorEnum::eRuntime, "unsupported device type");
    }

    return oci::LinuxDevice {
        path.c_str(), type, major(sb.st_rdev), minor(sb.st_rdev), sb.st_mode & ~S_IFMT, sb.st_uid, sb.st_gid};
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error FileSystem::CreateHostFSWhiteouts(const std::string& path, const std::vector<std::string>& hostBinds)
{
    try {
        auto destPath = fs::path(path);

        fs::create_directories(destPath);
        fs::permissions(destPath, cDirPermissions);

        for (const auto& entry : fs::directory_iterator("/")) {
            if (std::find_if(hostBinds.begin(), hostBinds.end(),
                    [&entry](const std::string& bind) { return entry.path() == fs::path("/") / bind; })
                != hostBinds.end()) {
                continue;
            }

            auto itemPath = destPath / entry.path();

            if (fs::exists(itemPath)) {
                continue;
            }

            LOG_DBG() << "Create rootfs white out" << Log::Field("path", itemPath.c_str());

            auto ret = mknod(itemPath.c_str(), S_IFCHR, makedev(0, 0));
            AOS_ERROR_CHECK_AND_THROW(ret, "can't create white out");
        }

        return ErrorEnum::eNone;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e, ErrorEnum::eRuntime));
    }
}

Error FileSystem::CreateMountPoints(const std::string& mountPointDir, const std::vector<Mount>& mounts)
{
    try {
        for (const auto& mount : mounts) {
            if (mount.mType == "proc" || mount.mType == "tmpfs" || mount.mType == "sysfs") {
                CreateMountPoint(mountPointDir, mount, true);
            } else if (mount.mType == "bind") {
                CreateMountPoint(mountPointDir, mount, fs::is_directory(mount.mSource.CStr()));
            }
        }

        return ErrorEnum::eNone;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e, ErrorEnum::eRuntime));
    }
}

Error FileSystem::MountServiceRootFS(const std::string& rootfsPath, const std::vector<std::string>& layers)
{
    try {
        auto mountPoint = fs::path(rootfsPath);

        fs::create_directories(mountPoint);
        fs::permissions(mountPoint, cDirPermissions);

        std::vector<fs::path> lowerDirs;

        std::transform(
            layers.begin(), layers.end(), std::back_inserter(lowerDirs), [](const auto& layer) { return layer; });

        MountOverlay(mountPoint, lowerDirs, "", "");

        return ErrorEnum::eNone;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e, ErrorEnum::eRuntime));
    }
}

Error FileSystem::UmountServiceRootFS(const std::string& rootfsPath)
{
    try {
        auto mountPoint = fs::path(rootfsPath);

        if (!fs::exists(mountPoint)) {
            return ErrorEnum::eNone;
        }

        UmountDir(mountPoint);
        fs::remove_all(mountPoint);

        return ErrorEnum::eNone;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e, ErrorEnum::eRuntime));
    }
}

Error FileSystem::PrepareServiceStorage(const std::string& path, uid_t uid, gid_t gid)
{
    try {
        auto storagePath = fs::path(path);

        if (fs::exists(storagePath)) {
            return ErrorEnum::eNone;
        }

        fs::create_directories(storagePath);
        fs::permissions(storagePath, cDirPermissions);

        auto ret = chown(storagePath.c_str(), uid, gid);
        AOS_ERROR_CHECK_AND_THROW(ret, "can't chown storage");

        return ErrorEnum::eNone;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e, ErrorEnum::eRuntime));
    }
}

Error FileSystem::PrepareServiceState(const std::string& path, uid_t uid, gid_t gid)
{
    try {
        auto statePath = fs::path(path);

        if (fs::exists(statePath)) {
            return ErrorEnum::eNone;
        }

        auto dirPath = statePath.parent_path();

        fs::create_directories(dirPath);
        fs::permissions(dirPath, cDirPermissions);

        std::ofstream file(statePath);
        fs::permissions(statePath, cStatePermissions);

        auto ret = chown(statePath.c_str(), uid, gid);
        AOS_ERROR_CHECK_AND_THROW(ret, "can't chown state");

        return ErrorEnum::eNone;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e, ErrorEnum::eRuntime));
    }
}

Error FileSystem::PrepareNetworkDir(const std::string& path)
{
    try {
        auto dirPath = fs::path(path) / "etc";

        fs::create_directories(dirPath);
        fs::permissions(dirPath, cDirPermissions);

        return ErrorEnum::eNone;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e, ErrorEnum::eRuntime));
    }
}

RetWithError<std::string> FileSystem::GetAbsPath(const std::string& path)
{
    try {
        return fs::absolute(path).string();
    } catch (const std::exception& e) {
        return {std::string(), AOS_ERROR_WRAP(common::utils::ToAosError(e, ErrorEnum::eRuntime))};
    }
}

RetWithError<gid_t> FileSystem::GetGIDByName(const std::string& groupName)
{
    auto group = getgrnam(groupName.c_str());
    if (group == nullptr) {
        return {0, Error(errno, "can't get group by name")};
    }

    return group->gr_gid;
}

Error FileSystem::PopulateHostDevices(const std::string& devicePath, std::vector<oci::LinuxDevice>& devices)
{
    try {
        auto devPath = fs::path(devicePath);

        if (!fs::is_directory(devPath)) {
            devices.push_back(DeviceFromPath(devPath));
        } else {
            for (const auto& entry : fs::recursive_directory_iterator(devPath,
                     fs::directory_options::follow_directory_symlink | fs::directory_options::skip_permission_denied)) {

                if (fs::is_directory(entry)) {
                    continue;
                }

                devices.push_back(DeviceFromPath(entry.path()));
            }
        }

        return ErrorEnum::eNone;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e, ErrorEnum::eRuntime));
    }
}

Error FileSystem::ClearDir(const std::string& path)
{
    try {
        auto dirPath = fs::path(path);

        fs::remove_all(dirPath);
        fs::create_directories(dirPath);
        fs::permissions(dirPath, cDirPermissions);

        return ErrorEnum::eNone;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e, ErrorEnum::eRuntime));
    }
}

Error FileSystem::RemoveAll(const std::string& path)
{
    try {
        auto dirPath = fs::path(path);

        fs::remove_all(dirPath);

        return ErrorEnum::eNone;
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e, ErrorEnum::eRuntime));
    }
}

RetWithError<std::vector<std::string>> FileSystem::ListDir(const std::string& path)
{
    try {
        std::vector<std::string> entries;
        auto                     dirPath = fs::path(path);

        for (const auto& entry : fs::directory_iterator(dirPath)) {
            if (!fs::is_directory(entry)) {
                continue;
            }

            entries.push_back(entry.path().filename().string());
        }

        return entries;
    } catch (const std::exception& e) {
        return {std::vector<std::string>(), AOS_ERROR_WRAP(common::utils::ToAosError(e, ErrorEnum::eRuntime))};
    }
}

} // namespace aos::sm::launcher
