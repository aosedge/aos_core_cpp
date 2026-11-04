/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_SMCONTROLLER_TESTS_STUBS_BLOBINFOPROVIDERSTUB_HPP_
#define AOS_CM_SMCONTROLLER_TESTS_STUBS_BLOBINFOPROVIDERSTUB_HPP_

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <core/common/imagemanager/itf/blobinfoprovider.hpp>

namespace aos::cm::smcontroller {

/**
 * Blob info provider stub.
 */
class BlobInfoProviderStub : public common::imagemanager::BlobInfoProviderItf {
public:
    /**
     * Sets blob URLs by its digest.
     *
     * @param digest blob digest.
     * @param urls blob URLs.
     */
    void SetBlobURLs(const std::string& digest, const std::vector<std::string>& urls)
    {
        std::lock_guard lock {mMutex};

        mBlobURLs[digest] = urls;
    }

    /**
     * Returns blob info.
     *
     * @param digest blob digest.
     * @param[out] blobInfo blob info.
     * @return Error.
     */
    Error GetBlobInfo(const String& digest, BlobInfo& blobInfo) override
    {
        std::lock_guard lock {mMutex};

        auto it = mBlobURLs.find(digest.CStr());
        if (it == mBlobURLs.end()) {
            return Error(ErrorEnum::eNotFound, "blob info not found");
        }

        blobInfo         = BlobInfo {};
        blobInfo.mDigest = digest;

        for (const auto& url : it->second) {
            if (auto err = blobInfo.mURLs.EmplaceBack(url.c_str()); !err.IsNone()) {
                return err;
            }
        }

        return ErrorEnum::eNone;
    }

private:
    std::mutex                                                mMutex;
    std::unordered_map<std::string, std::vector<std::string>> mBlobURLs;
};

} // namespace aos::cm::smcontroller

#endif
