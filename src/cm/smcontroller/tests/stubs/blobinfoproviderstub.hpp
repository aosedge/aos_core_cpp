/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_SMCONTROLLER_TESTS_STUBS_BLOBINFOPROVIDERSTUB_HPP_
#define AOS_CM_SMCONTROLLER_TESTS_STUBS_BLOBINFOPROVIDERSTUB_HPP_

#include <map>
#include <string>

#include <core/cm/imagemanager/itf/blobinfoprovider.hpp>

namespace aos::cm::smcontroller {

/**
 * Blob info provider stub.
 */
class BlobInfoProviderStub : public imagemanager::BlobInfoProviderItf {
public:
    /**
     * Sets blob info for a digest.
     *
     * @param digest blob digest.
     * @param blobInfo blob info.
     */
    void SetBlobInfo(const String& digest, const BlobInfo& blobInfo) { mBlobInfoMap[digest.CStr()] = blobInfo; }

    /**
     * Returns blobs info.
     *
     * @param digests list of blob digests.
     * @param[out] blobsInfo blobs info.
     * @return Error.
     */
    Error GetBlobsInfos(const Array<StaticString<oci::cDigestLen>>& digests, Array<BlobInfo>& blobsInfo) override
    {
        blobsInfo.Clear();

        for (const auto& digest : digests) {
            auto it = mBlobInfoMap.find(digest.CStr());
            if (it == mBlobInfoMap.end()) {
                return Error(ErrorEnum::eNotFound, "Blob info not found");
            }

            if (auto err = blobsInfo.EmplaceBack(it->second); !err.IsNone()) {
                return err;
            }
        }

        return ErrorEnum::eNone;
    }

private:
    std::map<std::string, BlobInfo> mBlobInfoMap;
};

} // namespace aos::cm::smcontroller

#endif
