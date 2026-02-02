/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <Poco/Base64Decoder.h>

#include <common/utils/exception.hpp>
#include <common/utils/time.hpp>

#include "blobs.hpp"
#include "common.hpp"

namespace aos::common::cloudprotocol {

namespace {

/***********************************************************************************************************************
 * Statics
 **********************************************************************************************************************/

std::string Base64Decode(const std::string& encoded)
{
    std::istringstream  encodedStream(encoded);
    Poco::Base64Decoder decoder(encodedStream);

    return std::string(std::istreambuf_iterator<char>(decoder), std::istreambuf_iterator<char>());
}

void DecryptInfoFromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, crypto::DecryptInfo& decryptInfo)
{
    auto err = decryptInfo.mBlockAlg.Assign(json.GetValue<std::string>("blockAlg").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse blockAlg");

    const auto iv = Base64Decode(json.GetValue<std::string>("blockIv"));
    err           = decryptInfo.mBlockIV.Assign(String(iv.c_str()).AsByteArray());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse blockIv");

    const auto key = Base64Decode(json.GetValue<std::string>("blockKey"));
    err            = decryptInfo.mBlockKey.Assign(String(key.c_str()).AsByteArray());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse blockKey");
}

void SignInfoFromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, crypto::SignInfo& signInfo)
{
    auto err = signInfo.mChainName.Assign(json.GetValue<std::string>("chainName").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse signInfo chainName");

    err = signInfo.mAlg.Assign(json.GetValue<std::string>("alg").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse signInfo alg");

    const auto value = Base64Decode(json.GetValue<std::string>("value"));
    err              = signInfo.mValue.Assign(String(value.c_str()).AsByteArray());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse signInfo value");

    const auto trustedTimestamp = json.GetOptionalValue<std::string>("trustedTimestamp");

    if (!trustedTimestamp.has_value()) {
        AOS_ERROR_THROW(ErrorEnum::eRuntime, "trustedTimestamp is missing in signInfo JSON");
    }

    Tie(signInfo.mTrustedTimestamp, err) = Time::UTC(trustedTimestamp->c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "trusted timestamp parsing error");

    common::utils::ForEach(json, "ocspValues", [&signInfo](const Poco::Dynamic::Var& value) {
        auto err = signInfo.mOCSPValues.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse ocsp value");

        err = signInfo.mOCSPValues.Back().Assign(value.convert<std::string>().c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse ocsp value");
    });
}

void BlobInfoFromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, BlobInfo& blobURLInfo)
{
    auto err = blobURLInfo.mDigest.Assign(json.GetValue<std::string>("digest").c_str());
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse digest");

    common::utils::ForEach(json, "urls", [&](const Poco::Dynamic::Var& value) {
        err = blobURLInfo.mURLs.EmplaceBack();
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse url");

        err = blobURLInfo.mURLs.Back().Assign(value.convert<std::string>().c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse url");
    });

    err = String(json.GetValue<std::string>("sha256").c_str()).HexToByteArray(blobURLInfo.mSHA256);
    AOS_ERROR_CHECK_AND_THROW(err, "can't parse sha256");

    blobURLInfo.mSize = json.GetValue<size_t>("size");

    if (json.Has("decryptInfo")) {
        blobURLInfo.mDecryptInfo.EmplaceValue();

        DecryptInfoFromJSON(json.GetObject("decryptInfo"), *blobURLInfo.mDecryptInfo);
    }

    if (json.Has("signInfo")) {
        blobURLInfo.mSignInfo.EmplaceValue();

        SignInfoFromJSON(json.GetObject("signInfo"), *blobURLInfo.mSignInfo);
    }
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error ToJSON(const BlobURLsRequest& blobURLsRequest, Poco::JSON::Object& json)
{
    constexpr MessageType cMessageType = MessageTypeEnum::eRequestBlobUrls;

    try {
        json.set("messageType", cMessageType.ToString().CStr());

        if (auto err = ToJSON(static_cast<const Protocol&>(blobURLsRequest), json); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        json.set("digests", common::utils::ToJsonArray(blobURLsRequest.mDigests, common::utils::ToStdString));
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error FromJSON(const common::utils::CaseInsensitiveObjectWrapper& json, BlobURLsInfo& blobURLsInfo)
{
    try {
        if (auto err = FromJSON(json, static_cast<Protocol&>(blobURLsInfo)); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        common::utils::ForEach(json, "items", [&blobURLsInfo](const auto& value) {
            auto err = blobURLsInfo.mItems.EmplaceBack();
            AOS_ERROR_CHECK_AND_THROW(err, "can't parse blob URL info");

            BlobInfoFromJSON(common::utils::CaseInsensitiveObjectWrapper(value), blobURLsInfo.mItems.Back());
        });
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::common::cloudprotocol
