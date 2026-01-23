/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_SMCONTROLLER_TESTS_STUBS_ITEMINFOPROVIDERSTUB_HPP_
#define AOS_CM_SMCONTROLLER_TESTS_STUBS_ITEMINFOPROVIDERSTUB_HPP_

#include <string>
#include <unordered_map>

#include <core/cm/imagemanager/itf/iteminfoprovider.hpp>

namespace aos::cm::smcontroller {

/**
 * Item info provider stub.
 */
class ItemInfoProviderStub : public imagemanager::ItemInfoProviderItf {
public:
    /**
     * Sets blob URL by its digest.
     *
     * @param digest blob digest.
     * @param url blob URL.
     */
    void SetBlobURL(const std::string& digest, const std::string& url) { mBlobURLMap[digest] = url; }

    /**
     * Returns update item index digest.
     *
     * @param itemID update item ID.
     * @param version update item version.
     * @param[out] digest result item digest.
     * @return Error.
     */
    Error GetIndexDigest(const String& itemID, const String& version, String& digest) const override
    {
        (void)itemID;
        (void)version;
        (void)digest;

        return ErrorEnum::eNone;
    }

    /**
     * Returns blob path by its digest.
     *
     * @param digest blob digest.
     * @param[out] path result blob path.
     * @return Error.
     */
    Error GetBlobPath(const String& digest, String& path) const override
    {
        (void)digest;
        (void)path;

        return ErrorEnum::eNone;
    }

    /**
     * Returns blob URL by its digest.
     *
     * @param digest blob digest.
     * @param[out] url result blob URL.
     * @return Error.
     */
    Error GetBlobURL(const String& digest, String& url) const override
    {
        auto it = mBlobURLMap.find(digest.CStr());
        if (it == mBlobURLMap.end()) {
            return Error(ErrorEnum::eNotFound, "blob URL not found");
        }

        url = it->second.c_str();

        return ErrorEnum::eNone;
    }

    /**
     * Returns item current version.
     *
     * @param itemID update item ID.
     * @param[out] version result item version.
     * @return Error.
     */
    Error GetItemCurrentVersion(const String& itemID, String& version) const override
    {
        (void)itemID;
        (void)version;

        return ErrorEnum::eNone;
    }

private:
    std::unordered_map<std::string, std::string> mBlobURLMap;
};

} // namespace aos::cm::smcontroller

#endif
