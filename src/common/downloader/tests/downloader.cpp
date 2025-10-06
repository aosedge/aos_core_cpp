/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <future>
#include <optional>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <core/common/tests/mocks/alertsmock.hpp>
#include <core/common/tests/utils/log.hpp>

#include <common/downloader/downloader.hpp>

#include "stubs/httpserverstub.hpp"

using namespace testing;

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class DownloaderTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        aos::tests::utils::InitLog();

        std::filesystem::create_directory("download");

        std::ofstream ofs("test_file.dat", std::ios::binary);
        ofs << "This is a test file";
    }

    void CreateLargeFile(const std::string& filename, size_t sizeMB)
    {
        std::ofstream ofs(filename, std::ios::binary);
        if (!ofs.is_open()) {
            FAIL() << "Failed to create large file: " << filename;
        }

        const size_t      bufferSize = 1024 * 1024;
        std::vector<char> buffer(bufferSize);

        for (size_t i = 0; i < bufferSize; ++i) {
            buffer[i] = static_cast<char>(i % 256);
        }

        for (size_t mb = 0; mb < sizeMB; ++mb) {
            ofs.write(buffer.data(), bufferSize);
            if (!ofs.good()) {
                FAIL() << "Failed to write to large file at MB " << mb;
            }
        }
    }

    void StartServer(const std::string& filename = "test_file.dat", int port = 8000, int delayMs = 0)
    {
        mServer.emplace(filename, port, delayMs);
        mServer->Start();
    }

    void StopServer() { mServer->Stop(); }

    void TearDown() override
    {
        std::remove("test_file.dat");
        std::remove("large_test_file.dat");
        std::remove(mFilePath.c_str());
    }

    std::optional<HTTPServer>                mServer;
    aos::common::downloader::Downloader      mDownloader;
    StrictMock<aos::alerts::AlertSenderMock> mAlertSender;
    std::string                              mFilePath = "download/test_file.dat";
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(DownloaderTest, Download)
{
    StartServer();

    auto err = mDownloader.Download("http://localhost:8000/test_file.dat", mFilePath.c_str());
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    EXPECT_TRUE(std::filesystem::exists(mFilePath));

    std::ifstream ifs(mFilePath, std::ios::binary);
    std::string   content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    EXPECT_EQ(content, "This is a test file");

    StopServer();
}

TEST_F(DownloaderTest, DownloadFileScheme)
{
    auto err = mDownloader.Download("file://test_file.dat", mFilePath.c_str());
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    EXPECT_TRUE(std::filesystem::exists(mFilePath));

    std::ifstream ifs(mFilePath, std::ios::binary);
    std::string   content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    EXPECT_EQ(content, "This is a test file");
}

TEST_F(DownloaderTest, DownloadLargeFileWithProgress)
{
    const size_t fileSizeMB = 1;

    CreateLargeFile("large_test_file.dat", fileSizeMB);

    mDownloader.Init(&mAlertSender, std::chrono::seconds {1});

    StartServer("large_test_file.dat", 8001, 350);

    EXPECT_CALL(mAlertSender, SendAlert(_)).Times(6);

    auto err = mDownloader.Download("http://localhost:8001/large_test_file.dat", mFilePath.c_str());

    EXPECT_EQ(err, aos::ErrorEnum::eNone);
    EXPECT_TRUE(std::filesystem::exists(mFilePath));

    std::ifstream ifs(mFilePath, std::ios::binary | std::ios::ate);
    auto          fileSize = ifs.tellg();

    EXPECT_EQ(fileSize, static_cast<std::streamsize>(fileSizeMB * 1024 * 1024));

    StopServer();
}
