/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include "utils/filesystem.hpp"

using namespace testing;

namespace fs = std::filesystem;

namespace aos::common::utils {

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST(MkTmpDirTest, CreatesDirectory)
{
    auto result = MkTmpDir("", "my_temp_dir.XXXXXX");

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

TEST(MkTmpDirTest, WithCustomDir)
{
    std::string customDir = std::filesystem::temp_directory_path() / "custom_temp_dir";
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

TEST(MkTmpDirTest, PatternHandling)
{
    auto result = MkTmpDir("", "test_dir");

    ASSERT_EQ(result.mError, aos::ErrorEnum::eNone);
    EXPECT_TRUE(std::filesystem::exists(result.mValue));
    EXPECT_TRUE(std::filesystem::is_directory(result.mValue));
    EXPECT_TRUE(result.mValue.find("test_dir.") != std::string::npos);

    std::filesystem::remove_all(result.mValue);

    result = MkTmpDir("", "another_test.XXXXXX");

    ASSERT_EQ(result.mError, aos::ErrorEnum::eNone);
    EXPECT_TRUE(std::filesystem::exists(result.mValue));
    EXPECT_TRUE(std::filesystem::is_directory(result.mValue));
    EXPECT_TRUE(result.mValue.find("another_test.") != std::string::npos);

    std::filesystem::remove_all(result.mValue);
}

} // namespace aos::common::utils
