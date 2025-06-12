/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FILECHUNKER_HPP_
#define FILECHUNKER_HPP_

#include <filesystem>
#include <string>
#include <vector>

#include <aos/common/tools/error.hpp>

namespace aos::mp::filechunker {

/**
 * Image content.
 */
struct ImageContent {
    uint64_t             mRequestID;
    std::string          mRelativePath;
    uint64_t             mPartsCount;
    uint64_t             mPart;
    std::vector<uint8_t> mData;
};

/**
 * Image file.
 */
struct ImageFile {
    std::string          mRelativePath;
    std::vector<uint8_t> mSha256;
    uint64_t             mSize;
};

/**
 * Content info.
 */
struct ContentInfo {
    uint64_t                  mRequestID;
    std::vector<ImageFile>    mImageFiles;
    std::vector<ImageContent> mImageContents;
};

/**
 * Chunks files.
 *
 * @param rootDir root directory.
 * @param requestID request ID.
 * @return RetWithError<ContentInfo>.
 */
RetWithError<ContentInfo> ChunkFiles(const std::string& rootDir, uint64_t requestID);

} // namespace aos::mp::filechunker

#endif // FILECHUNKER_HPP_
