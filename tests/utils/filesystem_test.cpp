/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include <aos/common/tools/fs.hpp>
#include <aos/test/log.hpp>

#include "utils/filesystem.hpp"

using namespace testing;

namespace fs = std::filesystem;

namespace aos::common::utils {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

namespace {

constexpr auto cTestDir = "fs_tests";

}

using namespace testing;

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class FSTest : public Test {
protected:
    void SetUp() override
    {
        FS::ClearDir(cTestDir);
        test::InitLog();
    }
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(FSTest, CreatesDirectory)
{
    auto result = MkTmpDir(cTestDir, "my_temp_dir.XXXXXX");

    ASSERT_EQ(result.mError, aos::ErrorEnum::eNone);
    EXPECT_TRUE(std::filesystem::exists(result.mValue));
    EXPECT_TRUE(std::filesystem::is_directory(result.mValue));
    EXPECT_TRUE(result.mValue.find("my_temp_dir.") != std::string::npos);

    std::filesystem::remove_all(result.mValue);

    result = MkTmpDir();

    ASSERT_EQ(result.mError, aos::ErrorEnum::eNone);
    EXPECT_TRUE(std::filesystem::exists(result.mValue));
    EXPECT_TRUE(std::filesystem::is_directory(result.mValue));
    EXPECT_TRUE(result.mValue.find("tmp.") != std::string::npos);

    std::filesystem::remove_all(result.mValue);
}

TEST_F(FSTest, WithCustomDir)
{
    std::string customDir = std::filesystem::path(cTestDir) / "custom_temp_dir";
    std::filesystem::create_directory(customDir);

    auto result = MkTmpDir(customDir, "my_temp_dir.XXXXXX");

    ASSERT_EQ(result.mError, aos::ErrorEnum::eNone);
    EXPECT_TRUE(std::filesystem::exists(result.mValue));
    EXPECT_TRUE(std::filesystem::is_directory(result.mValue));
    EXPECT_TRUE(result.mValue.find(customDir) != std::string::npos);
    EXPECT_TRUE(result.mValue.find("my_temp_dir.") != std::string::npos);

    std::filesystem::remove_all(result.mValue);

    result = MkTmpDir(customDir);

    ASSERT_EQ(result.mError, aos::ErrorEnum::eNone);
    EXPECT_TRUE(std::filesystem::exists(result.mValue));
    EXPECT_TRUE(std::filesystem::is_directory(result.mValue));
    EXPECT_TRUE(result.mValue.find(customDir) != std::string::npos);
    EXPECT_TRUE(result.mValue.find("tmp.") != std::string::npos);

    std::filesystem::remove_all(result.mValue);
    std::filesystem::remove_all(customDir);
}

TEST_F(FSTest, PatternHandling)
{
    auto result = MkTmpDir(cTestDir, "test_dir");

    ASSERT_EQ(result.mError, aos::ErrorEnum::eNone);
    EXPECT_TRUE(std::filesystem::exists(result.mValue));
    EXPECT_TRUE(std::filesystem::is_directory(result.mValue));
    EXPECT_TRUE(result.mValue.find("test_dir.") != std::string::npos);

    std::filesystem::remove_all(result.mValue);

    result = MkTmpDir(cTestDir, "another_test.XXXXXX");

    ASSERT_EQ(result.mError, aos::ErrorEnum::eNone);
    EXPECT_TRUE(std::filesystem::exists(result.mValue));
    EXPECT_TRUE(std::filesystem::is_directory(result.mValue));
    EXPECT_TRUE(result.mValue.find("another_test.") != std::string::npos);

    std::filesystem::remove_all(result.mValue);
}

TEST_F(FSTest, CalculateSize)
{
    std::array<char, 1024> buffer;

    const auto root = fs::path(cTestDir) / "size-test";
    const auto f1   = root / "f1";
    const auto f1s1 = f1 / "s1";
    const auto f2   = root / "f2";

    fs::create_directories(f1s1);
    fs::create_directories(f2);

    if (auto file = std::ofstream(root / "root.txt", std::ios::binary); file.good()) {
        file.write(buffer.data(), buffer.size());
    }

    if (auto file = std::ofstream(f1 / "f1.txt", std::ios::binary); file.good()) {
        file.write(buffer.data(), buffer.size());
    }

    if (auto file = std::ofstream(f1s1 / "f1s1.txt", std::ios::binary); file.good()) {
        file.write(buffer.data(), buffer.size());
    }

    if (auto file = std::ofstream(f2 / "f2.txt", std::ios::binary); file.good()) {
        file.write(buffer.data(), buffer.size());
    }

    auto [size, err] = CalculateSize(root.string());

    ASSERT_EQ(err, aos::ErrorEnum::eNone);
    EXPECT_EQ(size, 4 * buffer.size());
}

TEST_F(FSTest, ChangeOwner)
{
    const auto root = fs::path(cTestDir) / "change-owner-test";

    fs::create_directories(root);

    if (getuid() != 0 && getgid() != 0) {
        ASSERT_EQ(ChangeOwner(root.string(), 0, 0), aos::ErrorEnum::eFailed);
    }

    ASSERT_EQ(ChangeOwner(root.string(), getuid(), getgid()), aos::ErrorEnum::eNone);
}

} // namespace aos::common::utils
