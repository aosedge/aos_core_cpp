/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <cmath>
#include <filesystem>
#include <fstream>

#include <Poco/DigestEngine.h>
#include <Poco/DigestStream.h>
#include <Poco/SHA2Engine.h>
#include <Poco/StreamCopier.h>

#include <logger/logmodule.hpp>

#include "filechunker.hpp"

namespace aos::mp::filechunker {

/***********************************************************************************************************************
 * Constants
 **********************************************************************************************************************/

constexpr size_t cChunkSize = 1024;

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

static std::vector<ImageContent> GetChunkedFileContent(
    std::ifstream& file, uint64_t requestID, uint64_t partCounts, const std::string& relPath)
{
    std::vector<ImageContent> imageContents;
    uint64_t                  chunkNum = 1;

    while (file) {
        ImageContent imageContent {requestID, relPath, partCounts, chunkNum, std::vector<uint8_t>(cChunkSize)};

        file.read(reinterpret_cast<char*>(imageContent.mData.data()), cChunkSize);
        imageContent.mData.resize(static_cast<size_t>(file.gcount()));

        imageContents.push_back(std::move(imageContent));
        chunkNum++;
    }

    return imageContents;
}

static std::pair<ImageFile, std::vector<ImageContent>> PrepareImageInfo(
    const std::string& rootDir, const std::string& path, uint64_t requestID)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open file: " + path);
    }

    Poco::SHA2Engine         sha256;
    Poco::DigestOutputStream dos(sha256);
    Poco::StreamCopier::copyStream(file, dos);
    dos.close();

    std::vector<uint8_t> hash(sha256.digest().begin(), sha256.digest().end());

    file.clear();
    file.seekg(0, std::ios::beg);

    uint64_t    fileSize   = std::filesystem::file_size(path);
    uint64_t    partCounts = static_cast<uint64_t>(std::ceil(static_cast<double>(fileSize) / cChunkSize));
    std::string relPath    = std::filesystem::relative(path, rootDir).string();

    auto imageContents = GetChunkedFileContent(file, requestID, partCounts, relPath);

    ImageFile imageFile {relPath, hash, fileSize};

    return {imageFile, imageContents};
}

/***********************************************************************************************************************
 * Public functions
 **********************************************************************************************************************/

RetWithError<ContentInfo> ChunkFiles(const std::string& rootDir, uint64_t requestID)
{
    LOG_DBG() << "Chunking files: rootDir=" << rootDir.c_str();

    ContentInfo contentInfo;
    contentInfo.mRequestID = requestID;

    try {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(rootDir)) {
            if (entry.is_directory()) {
                continue;
            }

            auto [imageFile, imageContents] = PrepareImageInfo(rootDir, entry.path().string(), requestID);

            contentInfo.mImageFiles.push_back(std::move(imageFile));
            contentInfo.mImageContents.insert(
                contentInfo.mImageContents.end(), imageContents.begin(), imageContents.end());
        }
    } catch (const std::exception& e) {
        return {contentInfo, Error(ErrorEnum::eRuntime, e.what())};
    }

    return contentInfo;
}

} // namespace aos::mp::filechunker
