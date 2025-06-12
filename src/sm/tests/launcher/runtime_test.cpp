/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>

#include <gtest/gtest.h>

#include <aos/test/log.hpp>

#include "launcher/runtime.hpp"

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
        aos::test::InitLog();

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

} // namespace aos::sm::launcher
