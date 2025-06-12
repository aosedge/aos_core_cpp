/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>

#include "common/logger/logmodule.hpp"
#include "common/utils/exception.hpp"

#include "imageunpacker.hpp"
#include "serviceimage.hpp"

namespace aos::mp::imageunpacker {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

ImageUnpacker::ImageUnpacker(const std::string& imageStoreDir)
    : mImageStoreDir(imageStoreDir)
{
    LOG_DBG() << "Creating ImageUnpacker: imageStoreDir=" << imageStoreDir.c_str();

    try {
        if (!std::filesystem::exists(mImageStoreDir)) {
            std::filesystem::create_directories(mImageStoreDir);
        }
    } catch (const std::exception& e) {
        AOS_ERROR_THROW(ErrorEnum::eRuntime, "failed to create image store directory: " + std::string(e.what()));
    }
}

RetWithError<std::string> ImageUnpacker::Unpack(const std::string& archivePath, const std::string& contentType)
{
    LOG_DBG() << "Unpacking archive: archivePath=" << archivePath.c_str() << ", contentType=" << contentType.c_str();

    if (contentType == "service") {
        return UnpackService(archivePath, mImageStoreDir);
    }

    return {"", Error(ErrorEnum::eInvalidArgument, "invalid content type")};
}

} // namespace aos::mp::imageunpacker
