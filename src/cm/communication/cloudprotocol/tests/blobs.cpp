/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>

#include <cm/communication/cloudprotocol/blobs.hpp>

using namespace testing;

namespace aos::cm::communication::cloudprotocol {

namespace {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

} // namespace

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class CloudProtocolBlobs : public Test {
public:
    void SetUp() override { tests::utils::InitLog(); }
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(CloudProtocolBlobs, BlobURLsRequest)
{
    constexpr auto cExpectedMessage = R"({"messageType":"requestBlobUrls","correlationId":"id",)"
                                      R"("digests":[)"
                                      R"("sha256:3c3a4604a545cdc127456d94e421cd355bca5b528f4a9c1905b15da2eb4a4c6b",)"
                                      R"("sha256:1c3a4604a545cdc127456d94e421cd355bca5b528f4a9c1905b15da2eb4a4c6b"]})";

    auto request            = std::make_unique<BlobURLsRequest>();
    request->mCorrelationID = "id";
    request->mDigests.EmplaceBack("sha256:3c3a4604a545cdc127456d94e421cd355bca5b528f4a9c1905b15da2eb4a4c6b");
    request->mDigests.EmplaceBack("sha256:1c3a4604a545cdc127456d94e421cd355bca5b528f4a9c1905b15da2eb4a4c6b");

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(*request, *json);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(common::utils::Stringify(json), cExpectedMessage);
}

TEST_F(CloudProtocolBlobs, BlobURLsInfo)
{
    constexpr auto cJSON = R"({
        "messageType": "blobUrls",
        "correlationId": "id",
        "items": [
            {
                "digest": "sha256:3c3a4604a545cdc127456d94e421cd355bca5b528f4a9c1905b15da2eb4a4c6b",
                "urls": [
                    "http://example.com/image1.bin",
                    "http://backup.example.com/image1.bin"
                ],
                "sha256": "36f028580bb02cc8272a9a020f4200e346e276ae664e45ee80745574e2f5ab80",
                "size": 1000,
                "decryptInfo": {
                    "blockAlg": "AES256/CBC/pkcs7",
                    "blockIv": "YmxvY2tJdg==",
                    "blockKey": "YmxvY2tLZXk="
                },
                "signInfo": {
                    "chainName": "chainName",
                    "alg": "RSA/SHA256",
                    "value": "dmFsdWU=",
                    "trustedTimestamp": "2023-10-01T12:00:00Z",
                    "ocspValues": [
                        "ocspValue1",
                        "ocspValue2"
                    ]
                }
            }
        ]
    })";

    auto info = std::make_unique<BlobURLsInfo>();

    auto [jsonVar, err] = common::utils::ParseJson(cJSON);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    auto wrapper = common::utils::CaseInsensitiveObjectWrapper(jsonVar);

    err = FromJSON(wrapper, *info);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_STREQ(info->mCorrelationID.CStr(), "id");
    ASSERT_EQ(info->mItems.Size(), 1);

    const auto& image = info->mItems[0];

    EXPECT_STREQ(image.mURLs[0].CStr(), "http://example.com/image1.bin");
    EXPECT_STREQ(image.mURLs[1].CStr(), "http://backup.example.com/image1.bin");

    StaticArray<uint8_t, crypto::cSHA256Size> expectedSHA256;

    err = String("36f028580bb02cc8272a9a020f4200e346e276ae664e45ee80745574e2f5ab80").HexToByteArray(expectedSHA256);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(image.mSHA256, expectedSHA256);
    EXPECT_EQ(image.mSize, 1000);

    EXPECT_STREQ(image.mDecryptInfo.mBlockAlg.CStr(), "AES256/CBC/pkcs7");
    EXPECT_EQ(image.mDecryptInfo.mBlockIV, String("blockIv").AsByteArray());
    EXPECT_EQ(image.mDecryptInfo.mBlockKey, String("blockKey").AsByteArray());

    EXPECT_STREQ(image.mSignInfo.mChainName.CStr(), "chainName");
    EXPECT_STREQ(image.mSignInfo.mAlg.CStr(), "RSA/SHA256");
    EXPECT_EQ(image.mSignInfo.mValue, String("value").AsByteArray());
    EXPECT_EQ(image.mSignInfo.mTrustedTimestamp, Time::UTC("2023-10-01T12:00:00Z").mValue);
    ASSERT_EQ(image.mSignInfo.mOCSPValues.Size(), 2);
    EXPECT_STREQ(image.mSignInfo.mOCSPValues[0].CStr(), "ocspValue1");
    EXPECT_STREQ(image.mSignInfo.mOCSPValues[1].CStr(), "ocspValue2");
}

} // namespace aos::cm::communication::cloudprotocol
