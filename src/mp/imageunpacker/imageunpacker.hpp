/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_MP_IMAGEUNPACKER_IMAGEUNPACKER_HPP_
#define AOS_MP_IMAGEUNPACKER_IMAGEUNPACKER_HPP_

#include <string>

#include <core/common/tools/error.hpp>

namespace aos::mp::imageunpacker {

/**
 * Image unpacker.
 */
class ImageUnpacker {
public:
    /**
     * Constructor.
     *
     * @param imageStoreDir image store directory.
     */
    explicit ImageUnpacker(const std::string& imageStoreDir);

    /**
     * Unpacks archive.
     *
     * @param archivePath archive path.
     * @param contentType content type.
     * @return RetWithError<std::string>.
     */
    RetWithError<std::string> Unpack(const std::string& archivePath, const std::string& contentType = "service");

private:
    std::string mImageStoreDir;
};

} // namespace aos::mp::imageunpacker

#endif
