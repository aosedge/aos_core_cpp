/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/Pipe.h>
#include <Poco/PipeStream.h>
#include <Poco/Process.h>
#include <Poco/StreamCopier.h>
#include <gtest/gtest.h>

#include <core/common/crypto/cryptoprovider.hpp>
#include <core/common/tests/mocks/ocispecmock.hpp>
#include <core/common/tests/stubs/spaceallocatorstub.hpp>
#include <core/common/tests/utils/log.hpp>

#include <common/utils/json.hpp>
#include <sm/image/imagehandler.hpp>

using namespace testing;

namespace aos::sm::image {

namespace {

/***********************************************************************************************************************
 * Constants
 **********************************************************************************************************************/

constexpr auto cTestDirRoot         = "test_dir/image";
constexpr auto cConfigJSON          = R"({
    "created": "2024-11-14T13:50:53Z",
    "author": "Aos cloud",
    "architecture": "x86",
    "os": "linux",
    "config": {
        "Entrypoint": [
            "python3"
        ],
        "Cmd": [
            "-u",
            "main.py"
        ],
        "WorkingDir": "/"
    }
})";
constexpr auto cConfigDigest        = "a9fd89f4f021b5cd92fc993506886c243f024d4e4d863bc4939114c05c0b5f60";
constexpr auto cServiceJSON         = R"({
    "created": "2024-11-14T13:50:53Z",
    "author": "Aos cloud",
    "balancingPolicy": "enabled",
    "runner": "crun",
    "runners": [
        "runc",
        "crun"
    ],
    "quotas": {
        "cpuLimit": 3000,
        "cpuDmipsLimit": 3000,
        "ramLimit": 64000000,
        "storageLimit": 64000000
    }
})";
constexpr auto cServiceConfigDigest = "7bcbb9f29c1dd8e1d8a61eccdcf7eeeb3ec6072effdf6723707b5f4ead062e9c";
constexpr auto cPythonMain          = R"(def main():
    # Send information to HTTP server.
    while True:
        print("Hello, world!")
        sleep(10)
)";
constexpr auto cManifestJSON        = R"({"configDigest":"","layers":[],"serviceDigest":""})";
constexpr auto cExpectedLayerSize   = strlen(cPythonMain);
constexpr auto cExpectedServiceSize
    = strlen(cConfigJSON) + strlen(cServiceJSON) + strlen(cPythonMain) + strlen(cManifestJSON);

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

std::string Hash(const std::filesystem::path& path, crypto::Hash algorithm)
{
    Poco::Process::Args args;
    args.push_back("dgst");
    args.push_back(std::string("-").append(algorithm.ToString().CStr()));
    args.push_back(path);

    Poco::Pipe          outPipe;
    Poco::ProcessHandle ph = Poco::Process::launch("openssl", args, nullptr, &outPipe, &outPipe);
    int                 rc = ph.wait();

    std::string           output;
    Poco::PipeInputStream istr(outPipe);
    Poco::StreamCopier::copyToString(istr, output);

    if (rc != 0) {
        throw std::runtime_error("Failed to hash file with openssl: " + output);
    }

    if (output.empty()) {
        throw std::runtime_error("Failed to hash file with openssl: empty output");
    }

    const auto pos = output.find('=');
    if (pos == std::string::npos) {
        throw std::runtime_error("Failed to hash file with openssl: invalid output");
    }

    output = output.substr(pos + 1);
    output.erase(std::remove_if(output.begin(), output.end(), [](const auto ch) { return ch == ' ' || ch == '\n'; }),
        output.end());

    LOG_DBG() << "Hash: path=" << path.c_str() << ", algorithm=" << algorithm << ", digest=" << output.c_str();

    return output;
}

void CreateTestTarFile(
    const std::filesystem::path& tarPath, const std::string& contentFilePath, const std::string& content)
{
    const auto tmpPath = std::filesystem::path(cTestDirRoot) / "tmp-service-artifacts";

    std::filesystem::create_directories(tmpPath);

    std::ofstream ofs(tmpPath / contentFilePath);
    ofs << content;
    ofs.close();

    Poco::Process::Args args;
    args.push_back("-czf");
    args.push_back(tarPath);
    args.push_back("-C");
    args.push_back(tmpPath);
    args.push_back(".");

    Poco::Pipe          outPipe;
    Poco::ProcessHandle ph = Poco::Process::launch("tar", args, nullptr, &outPipe, &outPipe);
    int                 rc = ph.wait();

    std::filesystem::remove_all(tmpPath);

    if (rc != 0) {
        std::string           output;
        Poco::PipeInputStream istr(outPipe);
        Poco::StreamCopier::copyToString(istr, output);

        throw std::runtime_error("Failed to create test tar file: " + output);
    }

    std::filesystem::remove(contentFilePath);
}

[[maybe_unused]] void CreateTestTarFile(const std::string& tarPath, const std::string& contentRoot)
{
    Poco::Process::Args args;
    args.push_back("-czf");
    args.push_back(tarPath);
    args.push_back("-C");
    args.push_back(contentRoot);
    args.push_back(".");

    Poco::Pipe          outPipe;
    Poco::ProcessHandle ph = Poco::Process::launch("tar", args, nullptr, &outPipe, &outPipe);
    int                 rc = ph.wait();

    if (rc != 0) {
        std::string           output;
        Poco::PipeInputStream istr(outPipe);
        Poco::StreamCopier::copyToString(istr, output);

        LOG_DBG() << "Create test tar file: output=" << output.c_str();

        throw std::runtime_error("Failed to create test tar file: " + output);
    }
}

struct ImageMetadata {
    std::filesystem::path mArchivePath;
    std::string           mImageDigest;
    std::string           mConfigDigest;
    std::string           mServiceConfigDigest;
    std::string           mEmbeddedArchiveDigest;
};

RetWithError<ImageMetadata> CreateLayerArchive()
{
    auto root            = std::filesystem::path(cTestDirRoot) / "tmp" / "layer";
    auto manifest        = root / "manifest.json";
    auto embeddedArchive = root / "embedded-archive";

    ImageMetadata metadata = {};
    metadata.mArchivePath  = std::filesystem::path(cTestDirRoot) / "layer.tar.gz";

    std::filesystem::create_directories(root);

    CreateTestTarFile(embeddedArchive, "main.py", cPythonMain);

    metadata.mEmbeddedArchiveDigest = Hash(embeddedArchive, crypto::HashEnum::eSHA256);

    std::filesystem::rename(embeddedArchive, root / metadata.mEmbeddedArchiveDigest);

    if (std::ofstream of(manifest); of) {
        of << cManifestJSON;
    } else {
        return {{}, ErrorEnum::eFailed};
    }

    CreateTestTarFile(metadata.mArchivePath, root);

    metadata.mImageDigest = Hash(metadata.mArchivePath, crypto::HashEnum::eSHA3_256);

    return {metadata, ErrorEnum::eNone};
}

RetWithError<ImageMetadata> CreateServiceArchive()
{
    auto root              = std::filesystem::path(cTestDirRoot) / "tmp";
    auto blobs             = root / "blobs" / "sha256";
    auto manifest          = root / "manifest.json";
    auto configBlob        = blobs / "config";
    auto serviceConfigBlob = blobs / "service-config";
    auto embeddedArchive   = blobs / "embedded-archive";

    ImageMetadata metadata = {};
    metadata.mArchivePath  = std::filesystem::path(cTestDirRoot) / "service.tar.gz";

    std::filesystem::create_directories(blobs);

    CreateTestTarFile(embeddedArchive, "main.py", cPythonMain);

    if (std::ofstream of(manifest); of) {
        of << cManifestJSON;
    } else {
        return {{}, ErrorEnum::eFailed};
    }

    if (std::ofstream of(configBlob); of) {
        of << cConfigJSON;
    } else {
        return {{}, ErrorEnum::eFailed};
    }

    if (std::ofstream of(serviceConfigBlob); of) {
        of << cServiceJSON;
    } else {
        return {{}, ErrorEnum::eFailed};
    }

    metadata.mEmbeddedArchiveDigest = Hash(embeddedArchive, crypto::HashEnum::eSHA256);
    metadata.mConfigDigest          = Hash(configBlob, crypto::HashEnum::eSHA256);
    metadata.mServiceConfigDigest   = Hash(serviceConfigBlob, crypto::HashEnum::eSHA256);

    std::filesystem::rename(embeddedArchive, blobs / metadata.mEmbeddedArchiveDigest);
    std::filesystem::rename(configBlob, blobs / metadata.mConfigDigest);
    std::filesystem::rename(serviceConfigBlob, blobs / metadata.mServiceConfigDigest);

    CreateTestTarFile(metadata.mArchivePath, root);

    metadata.mImageDigest = Hash(metadata.mArchivePath, crypto::HashEnum::eSHA3_256);

    return {metadata, ErrorEnum::eNone};
}

LayerInfo CreateLayerInfo(const ImageMetadata& metadata)
{
    LayerInfo layerInfo = {};

    layerInfo.mLayerDigest = "sha256:";
    layerInfo.mLayerDigest.Append(metadata.mImageDigest.c_str());

    layerInfo.mSize    = std::filesystem::file_size(metadata.mArchivePath);
    layerInfo.mLayerID = "test-layer";

    auto err = String(metadata.mImageDigest.c_str()).HexToByteArray(layerInfo.mSHA256);
    if (!err.IsNone()) {
        LOG_ERR() << "Failed to convert digest to byte array: err=";
    }

    return layerInfo;
}

ServiceInfo CreateServiceInfo(const ImageMetadata& metadata)
{
    ServiceInfo serviceInfo = {};

    serviceInfo.mGID       = getgid();
    serviceInfo.mSize      = std::filesystem::file_size(metadata.mArchivePath);
    serviceInfo.mServiceID = "test-service";
    serviceInfo.mVersion   = "1.0.0";

    auto err = String(metadata.mImageDigest.c_str()).HexToByteArray(serviceInfo.mSHA256);
    if (!err.IsNone()) {
        LOG_ERR() << "Failed to convert digest to byte array: err=";
    }

    return serviceInfo;
}

} // namespace

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class ImageTest : public Test {
protected:
    void SetUp() override
    {
        aos::tests::utils::InitLog();

        ASSERT_TRUE(mCryptoProvider.Init().IsNone());

        std::filesystem::remove_all(cTestDirRoot);
    }

    void TearDown() override { std::filesystem::remove_all(cTestDirRoot); }

    aos::crypto::DefaultCryptoProvider mCryptoProvider;
    oci::OCISpecMock                   mOCISpec;
    spaceallocator::SpaceAllocatorStub mSpaceAllocator;
    ImageHandler                       mImageHandler;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(ImageTest, InstallLayer)
{
    auto [archiveMetadata, err] = CreateLayerArchive();

    ASSERT_TRUE(err.IsNone());

    ASSERT_TRUE(mImageHandler.Init(mCryptoProvider, mSpaceAllocator, mSpaceAllocator, mOCISpec, getuid()).IsNone());

    UniquePtr<aos::spaceallocator::SpaceItf> space;

    EXPECT_CALL(mOCISpec, LoadContentDescriptor)
        .WillOnce(Invoke([&archiveMetadata](const String&, oci::ContentDescriptor& descriptor) {
            descriptor.mDigest = "sha256:";
            descriptor.mDigest.Append(archiveMetadata.mEmbeddedArchiveDigest.c_str());

            return ErrorEnum::eNone;
        }));
    EXPECT_CALL(mOCISpec, SaveContentDescriptor).Times(0);

    const auto installRoot = std::filesystem::path(cTestDirRoot) / "install" / "layers";
    const auto layerInfo   = CreateLayerInfo(archiveMetadata);

    std::filesystem::create_directories(installRoot);

    StaticString<cFilePathLen> path;
    Tie(path, err)
        = mImageHandler.InstallLayer(archiveMetadata.mArchivePath.c_str(), installRoot.c_str(), layerInfo, space);

    ASSERT_TRUE(err.IsNone()) << "err= " << err.StrValue() << ", message=" << err.Message();

    ASSERT_NE(space.Get(), nullptr);
    EXPECT_EQ(space->Size(), cExpectedLayerSize);

    ASSERT_TRUE(fs::DirExist(path).mValue);
}

TEST_F(ImageTest, InstallService)
{
    auto [archiveMetadata, err] = CreateServiceArchive();

    ASSERT_TRUE(err.IsNone());

    ASSERT_TRUE(mImageHandler.Init(mCryptoProvider, mSpaceAllocator, mSpaceAllocator, mOCISpec, getuid()).IsNone());

    UniquePtr<aos::spaceallocator::SpaceItf> space;

    EXPECT_CALL(mOCISpec, LoadImageManifest)
        .WillOnce(Invoke([&archiveMetadata](const String&, oci::ImageManifest& manifest) {
            manifest.mConfig.mDigest = "sha256:";
            manifest.mConfig.mDigest.Append(archiveMetadata.mConfigDigest.c_str());

            manifest.mAosService.SetValue({});
            manifest.mAosService->mDigest = "sha256:";
            manifest.mAosService->mDigest.Append(archiveMetadata.mServiceConfigDigest.c_str());

            manifest.mLayers.PushBack({});
            manifest.mLayers[0].mDigest = "sha256:";
            manifest.mLayers[0].mDigest.Append(archiveMetadata.mEmbeddedArchiveDigest.c_str());

            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(mOCISpec, SaveImageManifest)
        .WillOnce(Invoke([](const String& path, const oci::ImageManifest& manifest) {
            LOG_DBG() << "Save image manifest: path=" << path << ", rootfsDigest=" << manifest.mLayers[0].mDigest;

            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(mOCISpec, LoadServiceConfig).Times(1);

    const auto installRoot = std::filesystem::path(cTestDirRoot) / "install" / "services";
    const auto serviceInfo = CreateServiceInfo(archiveMetadata);

    StaticString<cFilePathLen> path;
    Tie(path, err)
        = mImageHandler.InstallService(archiveMetadata.mArchivePath.c_str(), installRoot.c_str(), serviceInfo, space);

    ASSERT_TRUE(err.IsNone()) << "err= " << err.StrValue() << ", message=" << err.Message();

    ASSERT_NE(space.Get(), nullptr);
    EXPECT_EQ(space->Size(), cExpectedServiceSize);

    ASSERT_TRUE(fs::DirExist(path).mValue);
}

} // namespace aos::sm::image
