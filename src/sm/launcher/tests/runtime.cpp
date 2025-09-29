/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>

#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>

#include <sm/launcher/runtime.hpp>

using namespace testing;

namespace aos::sm::launcher {

namespace fs = std::filesystem;

namespace {

/***********************************************************************************************************************
 * Consts
 **********************************************************************************************************************/

constexpr auto cTestDirRoot = "test_dir/launcher";

} // namespace

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class LauncherTest : public Test {
protected:
    void SetUp() override
    {
        aos::tests::utils::InitLog();

        fs::remove_all(cTestDirRoot);
    }

    // void TearDown() override { fs::remove_all(cTestDirRoot); }

    Runtime mRuntime;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(LauncherTest, CreateHostFSWhiteouts)
{
    const char* const hostBindsData[] = {"bin", "sbin", "lib", "lib64", "usr"};

    StaticArray<StaticString<cFilePathLen>, cMaxNumHostBinds> hostBinds;

    for (const auto& bind : hostBindsData) {
        ASSERT_TRUE(hostBinds.PushBack(bind).IsNone());
    }

    auto whiteoutsPath = fs::path(cTestDirRoot) / "host" / "whiteouts";

    EXPECT_TRUE(mRuntime.CreateHostFSWhiteouts(whiteoutsPath.c_str(), hostBinds).IsNone());

    for (const auto& entry : fs::directory_iterator(whiteoutsPath)) {
        auto item = entry.path().filename();

        EXPECT_TRUE(fs::exists(fs::path("/") / item));

        auto status = fs::status(entry.path());

        EXPECT_TRUE(fs::is_character_file(status));
        EXPECT_EQ(status.permissions(), fs::perms::none);

        EXPECT_EQ(hostBinds.Find(item.c_str()), hostBinds.end());
    }
}

TEST_F(LauncherTest, PopulateHostDevices)
{
    const auto cRootDevicePath     = fs::path(cTestDirRoot) / "dev";
    const auto cTestDeviceFullPath = cRootDevicePath / "device1";

    if (!fs::exists(cRootDevicePath)) {
        fs::create_directories(cRootDevicePath);
    }

    if (auto res = mknod(cTestDeviceFullPath.c_str(), S_IFCHR, 0); res != 0) {
        FAIL() << "Can't create test device node: " << strerror(errno);
    }

    StaticArray<oci::LinuxDevice, 1> devices;

    auto err = mRuntime.PopulateHostDevices(cTestDeviceFullPath.c_str(), devices);
    EXPECT_TRUE(err.IsNone()) << "failed: " << tests::utils::ErrorToStr(err);

    EXPECT_EQ(devices.Size(), 1);
    EXPECT_STREQ(devices.Front().mPath.CStr(), cTestDeviceFullPath.c_str());
}

TEST_F(LauncherTest, PopulateHostDevicesSymlink)
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

    StaticArray<oci::LinuxDevice, 1> devices;

    auto err = mRuntime.PopulateHostDevices((cRootDevicePath / "link").c_str(), devices);
    EXPECT_TRUE(err.IsNone()) << "failed: " << tests::utils::ErrorToStr(err);

    EXPECT_EQ(devices.Size(), 1);
    EXPECT_STREQ(devices.Front().mPath.CStr(), cTestDeviceFullPath.c_str());
}

} // namespace aos::sm::launcher
