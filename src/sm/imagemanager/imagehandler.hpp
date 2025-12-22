/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_IMAGE_IMAGEHANDLER_HPP_
#define AOS_SM_IMAGE_IMAGEHANDLER_HPP_

#include <core/sm/imagemanager/itf/imagehandler.hpp>

namespace aos::sm::imagemanager {

/**
 * Image handler interface.
 */
class ImageHandler : public ImageHandlerItf {
public:
    /**
     * Destructor.
     */
    ~ImageHandler() = default;

    /**
     * Initializes image handler.
     *
     * @param uid user ID.
     * @param gid group ID.
     * @return Error.
     */
    Error Init(uid_t uid = 0, gid_t gid = 0)
    {
        mUID = uid;
        mGID = gid;

        return ErrorEnum::eNone;
    }

    /**
     * Unpacks layer to the destination path.
     *
     * @param src source layer path.
     * @param dst destination path.
     * @param mediaType layer media type.
     * @return Error.
     */
    Error UnpackLayer(const String& src, const String& dst, const String& mediaType) override;

    /**
     * Returns unpacked layer size.
     *
     * @param path packed layer path.
     * @param mediaType layer media type.
     * @return RetWithError<size_t>.
     */
    RetWithError<size_t> GetUnpackedLayerSize(const String& path, const String& mediaType) const override;

    /**
     * Returns unpacked layer digest.
     *
     * @param path unpacked layer path.
     * @return RetWithError<StaticString<oci::cDigestLen>>.
     */
    RetWithError<StaticString<oci::cDigestLen>> GetUnpackedLayerDigest(const String& path) const override;

private:
    Error CheckMediaType(const String& mediaType) const;

    uid_t mUID {};
    gid_t mGID {};
};

} // namespace aos::sm::imagemanager

#endif
