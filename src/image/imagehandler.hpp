/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IMAGEHANDLER_HPP_
#define IMAGEHANDLER_HPP_

#include <aos/sm/image/imagehandler.hpp>

namespace aos::sm::image {

/**
 * Image handler interface.
 */
class ImageHandler : public aos::sm::image::ImageHandlerItf {
public:
    /**
     * Installs layer from the provided archive.
     *
     * @param archivePath archive path.
     * @param installBasePath installation base path.
     * @param layer layer info.
     * @param space[out] installed layer space.
     * @return RetWithError<StaticString<cFilePathLen>>.
     */
    RetWithError<StaticString<cFilePathLen>> InstallLayer(const String& archivePath, const String& installBasePath,
        const LayerInfo& layer, UniquePtr<spaceallocator::SpaceItf>& space) override
    {
        (void)archivePath;
        (void)installBasePath;
        (void)layer;
        (void)space;

        return {""};
    }

    /**
     * Installs service from the provided archive.
     *
     * @param archivePath archive path.
     * @param installBasePath installation base path.
     * @param service service info.
     * @param space[out] installed service space.
     * @return RetWithError<StaticString<cFilePathLen>>.
     */
    RetWithError<StaticString<cFilePathLen>> InstallService(const String& archivePath, const String& installBasePath,
        const ServiceInfo& service, UniquePtr<spaceallocator::SpaceItf>& space) override
    {
        (void)archivePath;
        (void)installBasePath;
        (void)service;
        (void)space;

        return {""};
    }

    /**
     * Validates service.
     *
     * @param path service path.
     * @return Error.
     */
    Error ValidateService(const String& path) const override
    {
        (void)path;

        return ErrorEnum::eNone;
    }

    /**
     * Calculates digest for the given path or file.
     *
     * @param path root folder or file.
     * @return RetWithError<StaticString<cMaxDigestLen>>.
     */
    RetWithError<StaticString<oci::cMaxDigestLen>> CalculateDigest(const String& path) const override
    {
        (void)path;

        return {""};
    }
};

} // namespace aos::sm::image

#endif
