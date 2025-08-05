/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_IMAGE_IMAGEHANDLER_HPP_
#define AOS_SM_IMAGE_IMAGEHANDLER_HPP_

#include <core/common/crypto/crypto.hpp>
#include <core/common/tools/error.hpp>
#include <core/sm/image/imagehandler.hpp>

namespace aos::sm::image {

/**
 * Image handler interface.
 */
class ImageHandler : public ImageHandlerItf {
public:
    /**
     * Initializes image handler.
     *
     * @param hasher hasher.
     * @param layerSpaceAllocator layer space allocator.
     * @param serviceSpaceAllocator service space allocator.
     * @param ociSpec OCI spec.
     * @param uid default user id.
     * @return Error.
     */
    Error Init(crypto::HasherItf& hasher, spaceallocator::SpaceAllocatorItf& layerSpaceAllocator,
        spaceallocator::SpaceAllocatorItf& serviceSpaceAllocator, oci::OCISpecItf& ociSpec, uint32_t uid = 0);

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
        const LayerInfo& layer, UniquePtr<aos::spaceallocator::SpaceItf>& space) override;

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
        const ServiceInfo& service, UniquePtr<aos::spaceallocator::SpaceItf>& space) override;

    /**
     * Validates service.
     *
     * @param path service path.
     * @return Error.
     */
    Error ValidateService(const String& path) const override;

    /**
     * Calculates digest for the given path or file.
     *
     * @param path root folder or file.
     * @return RetWithError<StaticString<cMaxDigestLen>>.
     */
    RetWithError<StaticString<oci::cMaxDigestLen>> CalculateDigest(const String& path) const override;

    /**
     * Destructor.
     */
    ~ImageHandler() = default;

private:
    Error ValidateServiceConfig(const String& path, const String& digest) const;
    Error ValidateService(const String& path, const oci::ImageManifest& manifest) const;
    Error ValidateDigest(const String& path, const String& digest) const;
    RetWithError<StaticArray<uint8_t, cSHA256Size>> CalculateHash(const String& path, crypto::Hash algorithm) const;
    Error CheckFileInfo(const String& path, uint64_t size, const Array<uint8_t>& sha256) const;
    Error UnpackArchive(const String& source, const String& destination) const;
    Error PrepareServiceFS(const String& baseDir, const ServiceInfo& service, oci::ImageManifest& manifest,
        UniquePtr<aos::spaceallocator::SpaceItf>& space) const;

    crypto::HasherItf*                 mHasher                = nullptr;
    spaceallocator::SpaceAllocatorItf* mLayerSpaceAllocator   = nullptr;
    spaceallocator::SpaceAllocatorItf* mServiceSpaceAllocator = nullptr;
    mutable oci::OCISpecItf*           mOCISpec               = nullptr;
    uint32_t                           mUID                   = 0;
};

} // namespace aos::sm::image

#endif
