/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <future>
#include <optional>
#include <thread>

#include <gtest/gtest.h>

#include <aos/test/log.hpp>

#include "downloader/downloader.hpp"
#include "stubs/httpserverstub.hpp"

using namespace testing;

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class DownloaderTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        aos::test::InitLog();

        std::filesystem::create_directory("download");

        std::ofstream ofs("test_file.dat", std::ios::binary);
        ofs << "This is a test file";
        ofs.close();
    }

    void StartServer()
    {
        mServer.emplace("test_file.dat", 8000);
        mServer->Start();
    }

    void StopServer() { mServer->Stop(); }

    void TearDown() override
    {
        std::remove("test_file.dat");
        std::remove(mFilePath.c_str());
    }

    std::optional<HTTPServer>           mServer;
    aos::common::downloader::Downloader mDownloader;
    std::string                         mFilePath = "download/test_file.dat";
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(DownloaderTest, Download)
{
    StartServer();

    auto err = mDownloader.Download(
        "http://localhost:8000/test_file.dat", mFilePath.c_str(), aos::downloader::DownloadContentEnum::eService);
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    EXPECT_TRUE(std::filesystem::exists(mFilePath));

    std::ifstream ifs(mFilePath, std::ios::binary);
    std::string   content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    EXPECT_EQ(content, "This is a test file");

    StopServer();
}

TEST_F(DownloaderTest, DownloadFileScheme)
{
    auto err = mDownloader.Download(
        "file://test_file.dat", mFilePath.c_str(), aos::downloader::DownloadContentEnum::eService);
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    EXPECT_TRUE(std::filesystem::exists(mFilePath));

    std::ifstream ifs(mFilePath, std::ios::binary);
    std::string   content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    EXPECT_EQ(content, "This is a test file");
}
