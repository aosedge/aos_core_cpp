/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>
#include <grp.h>
#include <sys/stat.h>

#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>

#include <sm/launcher/runtimes/container/filesystem.hpp>

using namespace testing;

namespace aos::sm::launcher {

namespace fs = std::filesystem;

namespace {

/***********************************************************************************************************************
 * Consts
 **********************************************************************************************************************/

constexpr auto cTestDirRoot = "/tmp/test_dir/launcher";

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

void CreateFile(const fs::path& filePath, const std::string& payload)
{
    std::ofstream file(filePath);

    file << payload;
    file.close();
}

} // namespace

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class ContainerFileSystemTest : public Test {
protected:
    static void SetUpTestSuite() { tests::utils::InitLog(); }

    void SetUp() override { fs::remove_all(cTestDirRoot); }

    void TearDown() override { fs::remove_all(cTestDirRoot); }

    FileSystem mFileSystem;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(ContainerFileSystemTest, CreateHostFSWhiteouts)
{
    std::vector<std::string> hostBinds     = {"bin", "sbin", "lib", "lib64", "usr"};
    auto                     whiteoutsPath = fs::path(cTestDirRoot) / "host" / "whiteouts";

    auto err = mFileSystem.CreateHostFSWhiteouts(whiteoutsPath.string(), hostBinds);
    EXPECT_EQ(err, ErrorEnum::eNone) << "CreateHostFSWhiteouts failed: " << tests::utils::ErrorToStr(err);

    for (const auto& entry : fs::directory_iterator(whiteoutsPath)) {
        auto item = entry.path().filename();

        EXPECT_TRUE(fs::exists(fs::path("/") / item));

        auto status = fs::status(entry.path());

        EXPECT_TRUE(fs::is_character_file(status));
        EXPECT_EQ(status.permissions(), fs::perms::none);

        EXPECT_EQ(std::find(hostBinds.begin(), hostBinds.end(), item.string()), hostBinds.end());
    }
}

TEST_F(ContainerFileSystemTest, CreateMountPoints)
{
    std::vector<Mount> mounts = {
        Mount {"proc", "proc", "proc"},
        Mount {"tmpfs", "tmpfs", "tmpfs"},
        Mount {"sysfs", "sysfs", "sysfs"},
        Mount {"/etc/hosts", "etc/hosts", "bind", "rbind,ro"},
        Mount {"/var/log", "var/log", "bind", "rbind"},
        Mount {"/tmp", "tmp", "bind", "rw"},
    };

    auto mountPointDir = fs::path(cTestDirRoot) / "mountpoints";

    auto err = mFileSystem.CreateMountPoints(mountPointDir.string(), mounts);
    EXPECT_EQ(err, ErrorEnum::eNone) << "CreateMountPoints failed: " << tests::utils::ErrorToStr(err);

    for (const auto& mount : mounts) {
        auto mountPoint = mountPointDir / mount.mDestination.CStr();

        EXPECT_TRUE(fs::exists(mountPoint)) << "Mount point not created: " << mountPoint.string();

        if (mount.mType == "proc" || mount.mType == "tmpfs" || mount.mType == "sysfs") {
            EXPECT_TRUE(fs::is_directory(mountPoint));
        } else {
            if (fs::is_directory(mount.mSource.CStr())) {
                EXPECT_TRUE(fs::is_directory(mountPoint));
            } else {
                EXPECT_TRUE(fs::is_regular_file(mountPoint));
            }
        }
    }
}

TEST_F(ContainerFileSystemTest, PrepareNetworkDir)
{
    auto networkDir = fs::path(cTestDirRoot) / "network";

    auto err = mFileSystem.PrepareNetworkDir(networkDir.string());
    EXPECT_EQ(err, ErrorEnum::eNone) << "PrepareNetworkDir failed: " << tests::utils::ErrorToStr(err);

    EXPECT_TRUE(fs::exists(networkDir / "etc")) << "Network etc dir not created";
}

TEST_F(ContainerFileSystemTest, GetAbsPath)
{
    auto relativePath = fs::path("some") / "relative" / "path";
    auto expectedPath = fs::absolute(relativePath).string();

    auto [absPath, err] = mFileSystem.GetAbsPath(relativePath.string());
    EXPECT_EQ(err, ErrorEnum::eNone) << "GetAbsPath failed: " << tests::utils::ErrorToStr(err);

    EXPECT_EQ(absPath, expectedPath) << "Absolute path mismatch";
}

TEST_F(ContainerFileSystemTest, GetGIDByName)
{
    const std::string groupName = "nogroup";

    auto [gid, err] = mFileSystem.GetGIDByName(groupName);
    EXPECT_EQ(err, ErrorEnum::eNone) << "GetGIDByName failed: " << tests::utils::ErrorToStr(err);

    auto grp = getgrnam(groupName.c_str());
    EXPECT_NE(grp, nullptr) << "can't get group id";

    EXPECT_EQ(gid, grp->gr_gid) << "GID mismatch";
}

TEST_F(ContainerFileSystemTest, PopulateHostDevices)
{
    const auto cRootDevicePath     = fs::path(cTestDirRoot) / "dev";
    const auto cTestDeviceFullPath = cRootDevicePath / "device1";

    if (!fs::exists(cRootDevicePath)) {
        fs::create_directories(cRootDevicePath);
    }

    if (auto res = mknod(cTestDeviceFullPath.c_str(), S_IFCHR, 0); res != 0) {
        FAIL() << "Can't create test device node: " << strerror(errno);
    }

    std::vector<oci::LinuxDevice> devices;

    auto err = mFileSystem.PopulateHostDevices(cTestDeviceFullPath, devices);
    EXPECT_EQ(err, ErrorEnum::eNone) << "PopulateHostDevices failed: " << tests::utils::ErrorToStr(err);

    EXPECT_EQ(devices.size(), 1);
    EXPECT_EQ(devices.front().mPath, cTestDeviceFullPath.c_str());
}

TEST_F(ContainerFileSystemTest, PopulateHostDevicesSymlink)
{
    const auto cRootDevicePath     = fs::path(cTestDirRoot) / "dev";
    const auto cTestDeviceFullPath = cRootDevicePath / "device1";

    if (!fs::exists(cRootDevicePath)) {
        fs::create_directories(cRootDevicePath);
    }

    if (auto res = mknod(cTestDeviceFullPath.c_str(), S_IFCHR, 0); res != 0) {
        FAIL() << "Can't create test device node: " << strerror(errno);
    }

    const auto currentPath = fs::current_path();

    fs::current_path(cRootDevicePath);
    fs::create_symlink("device1", "link");
    fs::current_path(currentPath);

    std::vector<oci::LinuxDevice> devices;

    auto err = mFileSystem.PopulateHostDevices((cRootDevicePath / "link"), devices);
    EXPECT_EQ(err, ErrorEnum::eNone) << "PopulateHostDevices failed: " << tests::utils::ErrorToStr(err);

    EXPECT_EQ(devices.size(), 1);
    EXPECT_EQ(devices.front().mPath, (cRootDevicePath / "link").c_str());
}

TEST_F(ContainerFileSystemTest, ClearDir)
{
    const auto testDir = fs::path(cTestDirRoot) / "dir";

    fs::create_directories(testDir / "subdir");

    CreateFile(testDir / "file1", "test");
    CreateFile(testDir / "subdir" / "file2", "test");

    auto err = mFileSystem.ClearDir(testDir.string());
    EXPECT_EQ(err, ErrorEnum::eNone) << "ClearDir failed: " << tests::utils::ErrorToStr(err);

    EXPECT_TRUE(fs::exists(testDir)) << "Directory removed";
    EXPECT_TRUE(fs::is_empty(testDir)) << "Directory not empty";
}

TEST_F(ContainerFileSystemTest, RemoveAll)
{
    const auto testDir = fs::path(cTestDirRoot) / "dir";

    fs::create_directories(testDir / "subdir");

    CreateFile(testDir / "file1", "test");
    CreateFile(testDir / "subdir" / "file2", "test");

    auto err = mFileSystem.RemoveAll(testDir.string());
    EXPECT_EQ(err, ErrorEnum::eNone) << "RemoveAll failed: " << tests::utils::ErrorToStr(err);

    EXPECT_FALSE(fs::exists(testDir)) << "Directory not removed";
}

TEST_F(ContainerFileSystemTest, ListDir)
{
    const auto testDir = fs::path(cTestDirRoot) / "dir";

    fs::create_directories(testDir);
    fs::create_directories(testDir / "subdir1");
    fs::create_directories(testDir / "subdir2");
    fs::create_directories(testDir / "subdir3");

    CreateFile(testDir / "file1", "test");
    CreateFile(testDir / "file2", "test");

    auto [entries, err] = mFileSystem.ListDir(testDir.string());
    EXPECT_TRUE(err.IsNone()) << "ListDir failed: " << tests::utils::ErrorToStr(err);

    EXPECT_EQ(entries.size(), 3);
    EXPECT_NE(std::find(entries.begin(), entries.end(), "subdir1"), entries.end()) << "subdir1 not listed";
    EXPECT_NE(std::find(entries.begin(), entries.end(), "subdir2"), entries.end()) << "subdir2 not listed";
    EXPECT_NE(std::find(entries.begin(), entries.end(), "subdir3"), entries.end()) << "subdir3 not listed";
}

} // namespace aos::sm::launcher
