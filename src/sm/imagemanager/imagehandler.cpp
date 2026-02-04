/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

#include <core/common/tools/logger.hpp>

#include <common/utils/exception.hpp>
#include <common/utils/filesystem.hpp>
#include <common/utils/image.hpp>

#include "imagehandler.hpp"

namespace aos::sm::imagemanager {

namespace {

/***********************************************************************************************************************
 * Consts
 **********************************************************************************************************************/

constexpr auto cWhiteoutPrefix    = ".wh.";
constexpr auto cWhiteoutOpaqueDir = ".wh..wh..opq";

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

void OCIWhiteoutsToOverlay(const String& path)
{
    LOG_DBG() << "Convert OCI whiteouts to overlayfs" << Log::Field("path", path);

    for (const auto& entry : std::filesystem::recursive_directory_iterator(path.CStr())) {
        const std::string baseName = entry.path().filename().string();
        const std::string dirName  = entry.path().parent_path().string();

        if (entry.is_directory()) {
            continue;
        }

        if (baseName == cWhiteoutOpaqueDir) {
            if (auto res = setxattr(dirName.c_str(), "trusted.overlay.opaque", "y", 1, 0); res != 0) {
                AOS_ERROR_THROW(errno, "can't set opaque xattr");
            }

            std::filesystem::remove(entry.path());

            continue;
        }

        if (baseName.rfind(cWhiteoutPrefix, 0) == 0) {
            auto fullPath = std::filesystem::path(dirName) / baseName.substr(strlen(cWhiteoutPrefix));

            if (auto res = mknod(fullPath.c_str(), S_IFCHR, 0); res != 0) {
                AOS_ERROR_THROW(errno, "can't create whiteout node");
            }

            std::filesystem::remove(entry.path());

            continue;
        }
    }
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error ImageHandler::UnpackLayer(const String& src, const String& dst, const String& mediaType)
{
    try {

        LOG_DBG() << "Unpack layer" << Log::Field("src", src) << Log::Field("dst", dst)
                  << Log::Field("mediaType", mediaType);

        if (auto err = CheckMediaType(mediaType); !err.IsNone()) {
            return err;
        }

        std::filesystem::create_directory(dst.CStr());

        if (auto err = common::utils::UnpackTarImage(src.CStr(), dst.CStr()); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        OCIWhiteoutsToOverlay(dst);
        common::utils::ChangeOwner(dst.CStr(), mUID, mGID);
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

RetWithError<size_t> ImageHandler::GetUnpackedLayerSize(const String& path, const String& mediaType) const
{
    LOG_DBG() << "Get unpacked layer size" << Log::Field("path", path) << Log::Field("mediaType", mediaType);

    if (auto err = CheckMediaType(mediaType); !err.IsNone()) {
        return {0, err};
    }

    auto [size, err] = common::utils::GetUnpackedArchiveSize(path.CStr(), mediaType == oci::cMediaTypeLayerTarGZip);
    if (!err.IsNone()) {
        return {0, AOS_ERROR_WRAP(err)};
    }

    return size;
}

RetWithError<StaticString<oci::cDigestLen>> ImageHandler::GetUnpackedLayerDigest(const String& path) const
{
    LOG_DBG() << "Get unpacked layer digest" << Log::Field("path", path);

    auto [digest, err] = common::utils::CalculateDirDigest(path.CStr());
    if (!err.IsNone()) {
        return {StaticString<oci::cDigestLen>(""), AOS_ERROR_WRAP(err)};
    }

    StaticString<oci::cDigestLen> ociDigest;

    if (err = ociDigest.Assign(digest.c_str()); !err.IsNone()) {
        return {StaticString<oci::cDigestLen>(""), AOS_ERROR_WRAP(err)};
    }

    return ociDigest;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

Error ImageHandler::CheckMediaType(const String& mediaType) const
{
    if (mediaType != oci::cMediaTypeLayerTar && mediaType != oci::cMediaTypeLayerTarGZip) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eNotSupported, "unsupported layer media type"));
    }

    return ErrorEnum::eNone;
}

} // namespace aos::sm::imagemanager
