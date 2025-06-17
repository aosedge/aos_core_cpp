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

#include <common/utils/exception.hpp>
#include <common/utils/retry.hpp>
#include <sm/logger/logmodule.hpp>

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
constexpr auto cStatePermissions = fs::perms::owner_read | fs::perms::owner_write;

constexpr auto cMountRetryCount = 3;
constexpr auto cMountretryDelay = Time::cSeconds;

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

void MountDir(const fs::path& source, const fs::path& mountPoint, const std::string& fsType, unsigned long flags,
    const std::string& opts)
{
    LOG_DBG() << "Mount dir: source=" << source.c_str() << ", mountPoint=" << mountPoint.c_str()
              << ", type=" << fsType.c_str();

    auto err = common::utils::Retry(
        [&]() { return mount(source.c_str(), mountPoint.c_str(), fsType.c_str(), flags, opts.c_str()); },
        [&]([[maybe_unused]] int retryCount, [[maybe_unused]] Duration delay, const aos::Error& err) {
            LOG_WRN() << "Mount error: err=" << err << ", try remount...";

            sync();
            umount2(mountPoint.c_str(), MNT_FORCE);
        },
        cMountRetryCount, cMountretryDelay, Duration(0));
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
    LOG_DBG() << "Umount dir: mountPoint=" << mountPoint.c_str();

    auto err = common::utils::Retry(
        [&]() {
            sync();
            return umount(mountPoint.c_str());
        },
        [&]([[maybe_unused]] int retryCount, [[maybe_unused]] Duration delay, const aos::Error& err) {
            LOG_WRN() << "Umount error: err=" << err << ", retry...";

            umount2(mountPoint.c_str(), MNT_FORCE);
        },
        cMountRetryCount, cMountretryDelay, Duration(0));
    AOS_ERROR_CHECK_AND_THROW(err, "can't umount dir");
}

oci::LinuxDevice DeviceFromPath(const fs::path& path)
{
    auto devPath = path;

    if (fs::is_symlink(path)) {
        devPath = fs::read_symlink(path);
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
        devPath.c_str(), type, major(sb.st_rdev), minor(sb.st_rdev), sb.st_mode & ~S_IFMT, sb.st_uid, sb.st_gid};
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
            if (hostBinds.FindIf([&entry](const StaticString<cFilePathLen>& bind) {
                    return entry.path() == fs::path("/") / bind.CStr();
                })
                != hostBinds.end()) {
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
        return AOS_ERROR_WRAP(common::utils::ToAosError(e, ErrorEnum::eRuntime));
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
        return AOS_ERROR_WRAP(common::utils::ToAosError(e, ErrorEnum::eRuntime));
    }

    return ErrorEnum::eNone;
}

Error Runtime::MountServiceRootFS(const String& rootfsPath, const Array<StaticString<cFilePathLen>>& layers)
{
    try {
        auto mountPoint = fs::path(rootfsPath.CStr());

        fs::create_directories(mountPoint);
        fs::permissions(mountPoint, cDirPermissions);

        std::vector<fs::path> lowerDirs;

        std::transform(layers.begin(), layers.end(), std::back_inserter(lowerDirs),
            [](const auto& layer) { return layer.CStr(); });

        MountOverlay(mountPoint, lowerDirs, "", "");
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e, ErrorEnum::eRuntime));
    }

    return ErrorEnum::eNone;
}

Error Runtime::UmountServiceRootFS(const String& rootfsPath)
{
    try {
        auto mountPoint = fs::path(rootfsPath.CStr());

        UmountDir(mountPoint);
        fs::remove_all(mountPoint);
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e, ErrorEnum::eRuntime));
    }

    return ErrorEnum::eNone;
}

Error Runtime::PrepareServiceStorage(const String& path, uint32_t uid, uint32_t gid)
{
    try {
        auto storagePath = fs::path(path.CStr());

        if (fs::exists(storagePath)) {
            return ErrorEnum::eNone;
        }

        fs::create_directories(storagePath);

        auto ret = chown(storagePath.c_str(), uid, gid);
        AOS_ERROR_CHECK_AND_THROW(ret, "can't chown storage");

    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e, ErrorEnum::eRuntime));
    }

    return ErrorEnum::eNone;
}

Error Runtime::PrepareServiceState(const String& path, uint32_t uid, uint32_t gid)
{
    try {
        auto statePath = fs::path(path.CStr());

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
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e, ErrorEnum::eRuntime));
    }

    return ErrorEnum::eNone;
}

Error Runtime::PrepareNetworkDir(const String& path)
{
    try {
        auto dirPath = fs::path(path.CStr()) / "etc";

        fs::create_directories(dirPath);
        fs::permissions(dirPath, cDirPermissions);
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e, ErrorEnum::eRuntime));
    }

    return ErrorEnum::eNone;
}

RetWithError<StaticString<cFilePathLen>> Runtime::GetAbsPath(const String& path)
{
    try {
        return {fs::absolute(path.CStr()).c_str(), ErrorEnum::eNone};
    } catch (const std::exception& e) {
        return {"", AOS_ERROR_WRAP(common::utils::ToAosError(e, ErrorEnum::eRuntime))};
    }
}

RetWithError<uint32_t> Runtime::GetGIDByName(const String& groupName)
{
    try {
        auto group = getgrnam(groupName.CStr());

        return {group->gr_gid, ErrorEnum::eNone};

    } catch (const std::exception& e) {
        return {0, AOS_ERROR_WRAP(common::utils::ToAosError(e, ErrorEnum::eRuntime))};
    }
}

Error Runtime::PopulateHostDevices(const String& devicePath, Array<oci::LinuxDevice>& devices)
{
    try {
        auto devPath = fs::path(devicePath.CStr());

        if (!fs::is_directory(devPath)) {
            auto err = devices.PushBack(DeviceFromPath(devPath));
            AOS_ERROR_CHECK_AND_THROW(err, "can't populate host devices");
        } else {
            for (const auto& entry : fs::recursive_directory_iterator(devPath,
                     fs::directory_options::follow_directory_symlink | fs::directory_options::skip_permission_denied)) {

                if (fs::is_directory(entry)) {
                    continue;
                }

                auto err = devices.PushBack(DeviceFromPath(entry.path()));
                LOG_ERR() << "Can't populate host devices: err=" << err;
            }
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e, ErrorEnum::eRuntime));
    }

    return ErrorEnum::eNone;
}

} // namespace aos::sm::launcher
