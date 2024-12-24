/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef UTILS_IMAGE_HPP
#define UTILS_IMAGE_HPP

#include <string>

#include <aos/common/tools/error.hpp>

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
 * @return RetWithError<uint64_t>.
 */
RetWithError<uint64_t> GetUnpackedArchiveSize(const std::string& archivePath);

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

} // namespace aos::common::utils

#endif // UTILS_IMAGE_HPP
