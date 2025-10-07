/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_UTILS_IMAGE_HPP_
#define AOS_COMMON_UTILS_IMAGE_HPP_

#include <string>

#include <core/cm/imagemanager/itf/imageunpacker.hpp>
#include <core/common/tools/error.hpp>

namespace aos::common::utils {

using Digest = std::string;

/**
 * Unpacks an image archive.
 *
 * @param archivePath path to the archive.
 * @param destination path to the destination directory.
 * @return aos::Error.
 */
Error UnpackTarImage(const std::string& archivePath, const std::string& destination);

/**
 * Returns size of the unpacked archive.
 *
 * @param archivePath path to the archive.
 * @param isTarGz flag to indicate if the archive is tar.gz.
 * @return RetWithError<uint64_t>.
 */
RetWithError<uint64_t> GetUnpackedArchiveSize(const std::string& archivePath, bool isTarGz = true);

/**
 * Parses the digest string.
 *
 * @param digest digest string.
 * @return std::pair<std::string, std::string> .
 */
std::pair<std::string, std::string> ParseDigest(const std::string& digest);

/**
 * Validates the digest.
 *
 * @param digest digest string.
 */
Error ValidateDigest(const Digest& digest);

/**
 * Hashes the directory.
 *
 * @param dir directory path.
 * @return std::string.
 */
RetWithError<std::string> HashDir(const std::string& dir);

/**
 * Image unpacker interface.
 */
class ImageUnpacker : public cm::imagemanager::ImageUnpackerItf {
public:
    /**
     * Returns the size of the uncompressed file in the archive.
     *
     * @param path path to the archive file.
     * @param filePath path to the file in the archive.
     * @return RetWithError<size_t>.
     */
    RetWithError<size_t> GetUncompressedFileSize(const String& path, const String& filePath) override;

    /**
     * Extracts a file from an archive.
     *
     * @param archivePath path to the archive file.
     * @param filePath path to the file in the archive.
     * @param outputPath path to the output file.
     * @return Error.
     */
    Error ExtractFileFromArchive(const String& archivePath, const String& filePath, const String& outputPath) override;

private:
    static constexpr auto cFilePermissionStrLen     = 10;
    static constexpr auto cFilePermissionTokenIndex = 0;
    static constexpr auto cFileSizeTokenIndex       = 2;
    static constexpr auto cFileNameTokenIndex       = 5;
};

} // namespace aos::common::utils

#endif
