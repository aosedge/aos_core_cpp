/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>

#include <Poco/DigestStream.h>
#include <Poco/File.h>
#include <Poco/FileStream.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <Poco/JSON/Stringifier.h>
#include <Poco/Path.h>
#include <Poco/Process.h>
#include <Poco/SHA2Engine.h>
#include <Poco/StreamCopier.h>
#include <gtest/gtest.h>

#include "mp/imageunpacker/imageunpacker.hpp"

using namespace testing;

namespace fs = std::filesystem;

namespace aos::mp::imageunpacker {

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class ImageUnpackerTest : public ::testing::Test {
protected:
    std::string tmpDir;
    std::string aosSrvConfigDigest;

    void SetUp() override
    {
        tmpDir = fs::temp_directory_path() / fs::path("aos_test").string();
        fs::create_directories(tmpDir);
    }

    void TearDown() override { fs::remove_all(tmpDir); }

    std::string PrepareService()
    {
        std::string imageDir = tmpDir + "/image";
        fs::create_directories(imageDir + "/rootfs/home");

        std::ofstream(imageDir + "/rootfs/home/service.py").close();

        auto serviceSize = fs::file_size(imageDir + "/rootfs/home/service.py");

        std::string rootFsPath = imageDir + "/rootfs";
        std::string fsDigest   = GenerateFsLayer(imageDir, rootFsPath);

        std::vector<uint8_t> emptyData          = {};
        std::string          aosSrvConfigDigest = GenerateAndSaveDigest(imageDir + "/blobs", emptyData);

        GenerateImageManifest(imageDir, aosSrvConfigDigest, fsDigest, serviceSize);

        std::string archivePath = tmpDir + "/service.tar";
        PackImage(imageDir, archivePath);

        return archivePath;
    }

    bool FindSubdirectoryWithFile(
        const fs::path& directory, const std::string& targetSubDir, const std::string& targetFile)
    {
        for (const auto& entry : fs::recursive_directory_iterator(directory)) {
            if (entry.is_directory() && entry.path().filename() == targetSubDir) {
                fs::path targetPath = entry.path() / targetFile;
                if (fs::exists(targetPath)) {
                    return true;
                }
            }
        }

        return false;
    }

private:
    std::string GenerateAndSaveDigest(const std::string& path, const std::vector<uint8_t>& data)
    {
        Poco::SHA2Engine sha256;
        sha256.update(data.data(), data.size());
        std::string hashStr = Poco::DigestEngine::digestToHex(sha256.digest());

        fs::path digestPath = fs::path(path) / "sha256" / hashStr;
        fs::create_directories(digestPath.parent_path());
        std::ofstream(digestPath, std::ios::binary).write(reinterpret_cast<const char*>(data.data()), data.size());

        std::cerr << "Generated digest: " << hashStr << "for path: " << path << std::endl;

        return "sha256:" + hashStr;
    }

    void PackImage(const std::string& source, const std::string& name)
    {
        std::vector<std::string> args = {"-C", source, "-cf", name, "."};
        Poco::ProcessHandle      ph   = Poco::Process::launch("tar", args);
        int                      rc   = ph.wait();

        if (rc != 0) {
            throw std::runtime_error("Failed to create tar archive");
        }
    }

    std::string GenerateFsLayer(const std::string& imgFolder, const std::string& rootfs)
    {
        std::string blobsDir = imgFolder + "/blobs";
        fs::create_directories(blobsDir);

        std::string tarFile = blobsDir + "/_temp.tar.gz";

        std::vector<std::string> args = {"-C", rootfs, "-czf", tarFile, "."};
        Poco::ProcessHandle      ph   = Poco::Process::launch("tar", args);
        int                      rc   = ph.wait();

        if (rc != 0) {
            throw std::runtime_error("Failed to create tar archive: " + tarFile);
        }

        std::ifstream        ifs(tarFile, std::ios::binary);
        std::vector<uint8_t> byteValue((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        ifs.close();

        std::string digest = GenerateAndSaveDigest(blobsDir, byteValue);

        fs::remove_all(rootfs);

        return digest;
    }

    void GenerateImageManifest(const std::string& folderPath, const std::string& imgConfig,
        const std::string& rootfsLayer, size_t rootfsLayerSize)
    {
        Poco::JSON::Object::Ptr manifest = new Poco::JSON::Object;
        manifest->set("schemaVersion", 2);

        Poco::JSON::Object::Ptr config = new Poco::JSON::Object;
        config->set("mediaType", "application/vnd.oci.image.config.v1+json");
        config->set("digest", imgConfig);
        manifest->set("config", config);

        Poco::JSON::Array::Ptr  layers = new Poco::JSON::Array;
        Poco::JSON::Object::Ptr layer  = new Poco::JSON::Object;
        layer->set("mediaType", "application/vnd.oci.image.layer.v1.tar+gzip");
        layer->set("digest", rootfsLayer);
        layer->set("size", static_cast<int>(rootfsLayerSize));
        layers->add(layer);
        manifest->set("layers", layers);

        std::ofstream file(folderPath + "/manifest.json");
        Poco::JSON::Stringifier::stringify(manifest, file);
    }
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(ImageUnpackerTest, UnpackService)
{
    std::string archivePath = PrepareService();

    ImageUnpacker imageUnpacker(tmpDir + "/image_store");
    auto          result = imageUnpacker.Unpack(archivePath, "service");

    ASSERT_EQ(result.mError, ErrorEnum::eNone);
    EXPECT_TRUE(fs::exists(fs::path(result.mValue) / "manifest.json"));
    EXPECT_TRUE(fs::exists(fs::path(result.mValue) / "blobs" / "sha256" / aosSrvConfigDigest));
    EXPECT_TRUE(FindSubdirectoryWithFile(result.mValue, "home", "service.py"));
}

} // namespace aos::mp::imageunpacker
