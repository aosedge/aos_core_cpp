/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>

#include <Poco/SHA2Engine.h>

#include <gtest/gtest.h>

#include <mp/filechunker/filechunker.hpp>

using namespace testing;

namespace aos::mp::filechunker {

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class FileChunkerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        mTestDir      = "test_dir";
        mTestFilePath = mTestDir + "/test_file.txt";

        std::filesystem::create_directory(mTestDir);

        mContent = "This is a test file for file chunker.";

        std::ofstream ofs(mTestFilePath, std::ios::binary);

        ofs.write(mContent.c_str(), mContent.size());
    }

    void TearDown() override { std::filesystem::remove_all(mTestDir); }

    std::vector<uint8_t> ComputeSHA256(const std::string& content)
    {
        Poco::SHA2Engine sha256;
        sha256.update(content);

        return std::vector<uint8_t>(sha256.digest().begin(), sha256.digest().end());
    }

    std::string mTestDir;
    std::string mContent;
    std::string mTestFilePath;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(FileChunkerTest, ChunkFiles)
{
    uint64_t requestID = 1;
    auto     result    = ChunkFiles(mTestDir, requestID);

    ASSERT_EQ(result.mError, ErrorEnum::eNone);
    ASSERT_EQ(result.mValue.mRequestID, requestID);
    ASSERT_EQ(result.mValue.mImageFiles.size(), 1);
    ASSERT_EQ(result.mValue.mImageContents.size(), 1);

    auto& imageFile    = result.mValue.mImageFiles[0];
    auto& imageContent = result.mValue.mImageContents[0];

    auto     expectedSHA256 = ComputeSHA256(mContent);
    uint64_t expectedSize   = std::filesystem::file_size(mTestFilePath);

    EXPECT_EQ(imageFile.mRelativePath, "test_file.txt");
    EXPECT_EQ(imageFile.mSha256, expectedSHA256);
    EXPECT_EQ(imageFile.mSize, expectedSize);

    EXPECT_EQ(imageContent.mRequestID, requestID);
    EXPECT_EQ(imageContent.mRelativePath, "test_file.txt");
    EXPECT_EQ(imageContent.mPartsCount, 1);
    EXPECT_EQ(imageContent.mPart, 1);
    EXPECT_EQ(imageContent.mData.size(), 37);
}

} // namespace aos::mp::filechunker
