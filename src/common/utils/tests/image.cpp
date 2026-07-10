/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include <core/common/tests/utils/utils.hpp>

#include <common/utils/image.hpp>
#include <common/utils/utils.hpp>

using namespace testing;

namespace fs = std::filesystem;

namespace aos::common::utils {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

namespace {

void CreateTestTarFile(const std::string& tarPath, const std::string& contentFilePath, const std::string& content)
{
    std::ofstream ofs(contentFilePath);
    ofs << content;
    ofs.close();

    auto [_, err] = ExecCommand({"tar", "czf", tarPath, contentFilePath});
    EXPECT_TRUE(err.IsNone()) << "Failed to create test tar file: " << tests::utils::ErrorToStr(err);

    fs::remove(contentFilePath);
}

void CreateLargeTestTarFile(
    const std::string& tarPath, const std::string& contentDir, size_t fileCount, size_t fileSize)
{
    fs::create_directory(contentDir);

    for (size_t i = 0; i < fileCount; ++i) {
        std::string filePath = contentDir + "/file_" + std::to_string(i) + ".bin";

        std::ofstream(filePath).close();
        fs::resize_file(filePath, fileSize);
    }

    auto [_, err] = ExecCommand({"tar", "czf", tarPath, contentDir});
    EXPECT_TRUE(err.IsNone()) << "Failed to create test tar file: " << tests::utils::ErrorToStr(err);

    fs::remove_all(contentDir);
}

} // namespace

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST(UnpackTarImageTest, UnpackTarImageSuccess)
{
    std::string archivePath     = "test_archive.tar";
    std::string contentFilePath = "test_content.txt";
    std::string destination     = "test_unpack_dir";
    std::string fileContent     = "This is a test content";

    CreateTestTarFile(archivePath, contentFilePath, fileContent);

    auto [upackedSize, err] = GetUnpackedArchiveSize(archivePath);

    EXPECT_TRUE(err.IsNone()) << err.StrValue();
    EXPECT_EQ(upackedSize, fileContent.length());

    fs::create_directory(destination);

    err = UnpackTarImage(archivePath, destination);

    ASSERT_EQ(err, ErrorEnum::eNone);
    EXPECT_TRUE(fs::exists(destination + "/" + contentFilePath));

    fs::remove(archivePath);
    fs::remove_all(destination);
}

TEST(UnpackTarImageTest, GetUnpackedArchiveSizeTarGz)
{
    constexpr size_t   cFileCount = 3000;
    constexpr size_t   cFileSize  = 100000;
    constexpr uint64_t cTotalSize = static_cast<uint64_t>(cFileCount) * cFileSize;

    std::string archivePath = "test_large_archive.tar.gz";
    std::string contentDir  = "test_large_content";

    CreateLargeTestTarFile(archivePath, contentDir, cFileCount, cFileSize);

    auto [upackedSize, err] = GetUnpackedArchiveSize(archivePath, true);

    EXPECT_TRUE(err.IsNone()) << err.StrValue();
    EXPECT_EQ(upackedSize, cTotalSize);

    fs::remove(archivePath);
}

TEST(UnpackTarImageTest, UnpackTarImageFailure)
{
    std::string archivePath = "test_archive.tar";
    std::string destination = "test_unpack_dir";

    std::ofstream ofs(archivePath);
    ofs << "test_content";
    ofs.close();

    auto [upackedSize, err] = GetUnpackedArchiveSize(archivePath);

    EXPECT_EQ(err, ErrorEnum::eRuntime);
    EXPECT_EQ(upackedSize, 0);

    std::filesystem::create_directory(destination);

    err = UnpackTarImage(archivePath, destination);

    ASSERT_EQ(err, ErrorEnum::eRuntime);
    ASSERT_NE(err.Message(), "");

    fs::remove(archivePath);
    fs::remove_all(destination);
}

TEST(UnpackTarImageTest, SourceFileDoesNotExist)
{
    std::string archivePath = "non_existent_file.tar";
    std::string destination = "test_unpack_dir";

    auto result = UnpackTarImage(archivePath, destination);

    ASSERT_EQ(result, ErrorEnum::eNotFound);
    ASSERT_NE(result.Message(), "");
}

TEST(ParseDigestTest, ParseDigestSuccess)
{
    std::string digest = "sha256:1234567890abcdef";

    auto result = ParseDigest(digest);

    ASSERT_EQ(result.first, "sha256");
    ASSERT_EQ(result.second, "1234567890abcdef");
}

TEST(ParseDigestTest, ParseDigestNoSeparator)
{
    std::string digest = "1234567890abcdef";

    auto result = ParseDigest(digest);

    ASSERT_EQ(result.first, "1234567890abcdef");
    ASSERT_EQ(result.second, "");
}

TEST(ValidateDigestTest, ValidateDigestSuccess)
{
    std::string digest = "sha256:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

    auto result = ValidateDigest(digest);

    ASSERT_EQ(result, ErrorEnum::eNone);
}

TEST(ValidateDigestTest, ValidateDigestInvalidLength)
{
    std::string digest = "sha256:1234567890abcdef1234567890abcdef";

    auto result = ValidateDigest(digest);

    ASSERT_EQ(result, ErrorEnum::eInvalidArgument);
    ASSERT_NE(result.Message(), "");
}

TEST(ImageTest, CalculateDirDigest)
{
    std::string dir         = "test_dir";
    std::string fileContent = "This is a test content";
    std::string file1       = dir + "/file1.txt";
    std::string file2       = dir + "/file2.txt";

    fs::create_directory(dir);

    std::ofstream ofs1(file1);
    ofs1 << fileContent;
    ofs1.close();

    std::ofstream ofs2(file2);
    ofs2 << fileContent;
    ofs2.close();

    auto result = CalculateDirDigest(dir);

    ASSERT_EQ(result.mError, ErrorEnum::eNone);
    auto [algorithm, hex] = ParseDigest(result.mValue);

    ASSERT_EQ(algorithm, "sha256");
    ASSERT_NE(hex, "");

    auto res = ValidateDigest(result.mValue);

    ASSERT_EQ(res, ErrorEnum::eNone);

    fs::remove_all(dir);
}

} // namespace aos::common::utils
