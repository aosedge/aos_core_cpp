/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>

#include <Poco/Pipe.h>
#include <Poco/PipeStream.h>
#include <Poco/Process.h>
#include <Poco/StreamCopier.h>

#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>

#include <common/utils/filesystem.hpp>
#include <common/utils/image.hpp>
#include <sm/imagemanager/imagehandler.hpp>

using namespace testing;

namespace aos::sm::imagemanager {

namespace {

/***********************************************************************************************************************
 * Constants
 **********************************************************************************************************************/

constexpr auto cTestDirRoot = "/tmp/imagemanager_test";

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

void CreateTarGzArchive(const std::filesystem::path& sourceDir, const std::filesystem::path& archivePath)
{
    Poco::Process::Args args;

    args.push_back("-czf");
    args.push_back(archivePath.string());
    args.push_back("-C");
    args.push_back(sourceDir.string());
    args.push_back(".");

    Poco::Pipe outPipe;

    auto ph = Poco::Process::launch("tar", args, nullptr, &outPipe, &outPipe);

    int rc = ph.wait();
    if (rc != 0) {
        std::string output;

        Poco::PipeInputStream istr(outPipe);
        Poco::StreamCopier::copyToString(istr, output);

        throw std::runtime_error("failed to create test tar.gz file: " + output);
    }

    std::filesystem::remove_all(sourceDir);
}

void CreateFileWithContent(const std::filesystem::path& filePath, const std::string& content)
{
    std::ofstream ofs(filePath);

    ofs << content;
    ofs.close();
}

void CreateTestLayerContent(const std::filesystem::path& path)
{
    std::filesystem::create_directories(path);

    CreateFileWithContent(path / "file1.txt", "This is file 1");
    CreateFileWithContent(path / "file2.txt", "This is file 2");
    CreateFileWithContent(path / "file3.txt", "This is file 3");

    std::filesystem::create_directory(path / "dir1");

    CreateFileWithContent(path / "dir1" / "file4.txt", "This is file 4 in dir1");
    CreateFileWithContent(path / "dir1" / "file5.txt", "This is file 5 in dir1");
    CreateFileWithContent(path / "dir1" / "file6.txt", "This is file 6 in dir1");

    std::filesystem::create_directory(path / "dir2");

    CreateFileWithContent(path / "dir2" / "file7.txt", "This is file 7 in dir2");
    CreateFileWithContent(path / "dir2" / "file8.txt", "This is file 8 in dir2");
    CreateFileWithContent(path / "dir2" / "file9.txt", "This is file 9 in dir2");
}

} // namespace

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class ImageManagerTest : public Test {
protected:
    static void SetUpTestSuite() { tests::utils::InitLog(); }

    void SetUp() override
    {
        auto err = mImageHandler.Init(getuid(), getgid());
        ASSERT_TRUE(err.IsNone()) << "Failed to init image handler: " << tests::utils::ErrorToStr(err);
    }

    void TearDown() override { std::filesystem::remove_all(cTestDirRoot); }

    ImageHandler mImageHandler;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(ImageManagerTest, UnpackLayer)
{
    auto layerPath = std::filesystem::path(cTestDirRoot) / "input-layer";

    CreateTestLayerContent(layerPath.string());

    auto [layerDigest, err] = common::utils::CalculateDirDigest(layerPath.string());
    EXPECT_TRUE(err.IsNone()) << "Failed to calculate test layer digest: " << tests::utils::ErrorToStr(err);

    size_t layerSize;

    Tie(layerSize, err) = common::utils::CalculateSize(layerPath.string());
    EXPECT_TRUE(err.IsNone()) << "Failed to calculate test layer size: " << tests::utils::ErrorToStr(err);

    auto archivePath = std::filesystem::path(cTestDirRoot) / "layer.tar.gz";

    CreateTarGzArchive(layerPath, archivePath);

    size_t unpackedSize;

    Tie(unpackedSize, err) = mImageHandler.GetUnpackedLayerSize(archivePath.c_str(), oci::cOCILayerTarGZip);
    EXPECT_TRUE(err.IsNone()) << "Failed to get unpacked layer size: " << tests::utils::ErrorToStr(err);

    EXPECT_EQ(unpackedSize, layerSize) << "Unpacked layer size mismatch, expected: " << layerSize
                                       << ", got: " << unpackedSize;

    auto unpackedPath = std::filesystem::path(cTestDirRoot) / "unpacked-layer";

    err = mImageHandler.UnpackLayer(archivePath.c_str(), unpackedPath.string().c_str(), oci::cOCILayerTarGZip);
    EXPECT_TRUE(err.IsNone()) << "Failed to unpack layer: " << tests::utils::ErrorToStr(err);

    StaticString<oci::cDigestLen> unpackedDigest;

    Tie(unpackedDigest, err) = mImageHandler.GetUnpackedLayerDigest(unpackedPath.string().c_str());
    EXPECT_TRUE(err.IsNone()) << "Failed to get unpacked layer digest: " << tests::utils::ErrorToStr(err);

    EXPECT_EQ(unpackedDigest, layerDigest.c_str())
        << "Unpacked layer digest mismatch, expected: " << layerDigest.c_str() << ", got: " << unpackedDigest.CStr();
}

} // namespace aos::sm::imagemanager
