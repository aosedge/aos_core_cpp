/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>

#include <cm/communication/cloudprotocol/certificates.hpp>

using namespace testing;

namespace aos::cm::communication::cloudprotocol {

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class CloudProtocolCertificates : public Test {
public:
    void SetUp() override { tests::utils::InitLog(); }
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(CloudProtocolCertificates, RenewCertsNotification)
{
    constexpr auto cJSON = R"({
        "messageType": "renewCertificatesNotification",
        "certificates": [
            {
                "type": "iam",
                "node": {
                    "id": "node1"
                },
                "serial": "serial_1"
            },
            {
                "type": "offline",
                "node": {
                    "id": "node2"
                },
                "serial": "serial_2",
                "validTill": "2024-01-31T12:00:00Z"
            },
            {
                "type": "cm",
                "node": {
                    "id": "node3"
                },
                "serial": ""
            }
        ],
        "unitSecrets": {
            "version": "v1.0.0",
            "nodes": [
                {
                    "node": {
                        "id": "node1"
                    },
                    "secret": "secret_1"
                },
                {
                    "node": {
                        "id": "node2"
                    },
                    "secret": "secret_2"
                }
            ]
        }
    })";

    auto parsedNotification = std::make_unique<RenewCertsNotification>();

    auto [jsonVar, err] = common::utils::ParseJson(cJSON);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    auto wrapper = common::utils::CaseInsensitiveObjectWrapper(jsonVar);

    err = FromJSON(wrapper, *parsedNotification);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    ASSERT_EQ(parsedNotification->mCertificates.Size(), 3);
    ASSERT_EQ(parsedNotification->mUnitSecrets.mNodes.Size(), 2);
    ASSERT_EQ(parsedNotification->mUnitSecrets.mVersion, "v1.0.0");

    EXPECT_EQ(parsedNotification->mCertificates[0].mType, CertTypeEnum::eIAM);
    EXPECT_EQ(parsedNotification->mCertificates[0].mNodeID, "node1");
    EXPECT_EQ(parsedNotification->mCertificates[0].mSerial, "serial_1");
    EXPECT_FALSE(parsedNotification->mCertificates[0].mValidTill.HasValue());

    EXPECT_EQ(parsedNotification->mCertificates[1].mType, CertTypeEnum::eOffline);
    EXPECT_EQ(parsedNotification->mCertificates[1].mNodeID, "node2");
    EXPECT_EQ(parsedNotification->mCertificates[1].mSerial, "serial_2");
    ASSERT_TRUE(parsedNotification->mCertificates[1].mValidTill.HasValue());
    EXPECT_STREQ(parsedNotification->mCertificates[1].mValidTill->ToUTCString().mValue.CStr(), "2024-01-31T12:00:00Z");

    EXPECT_EQ(parsedNotification->mCertificates[2].mType, CertTypeEnum::eCM);
    EXPECT_EQ(parsedNotification->mCertificates[2].mNodeID, "node3");
    EXPECT_EQ(parsedNotification->mCertificates[2].mSerial, "");
    EXPECT_FALSE(parsedNotification->mCertificates[2].mValidTill.HasValue());

    EXPECT_EQ(parsedNotification->mUnitSecrets.mNodes[0].mNodeID, "node1");
    EXPECT_EQ(parsedNotification->mUnitSecrets.mNodes[0].mSecret, "secret_1");
    EXPECT_EQ(parsedNotification->mUnitSecrets.mNodes[1].mNodeID, "node2");
    EXPECT_EQ(parsedNotification->mUnitSecrets.mNodes[1].mSecret, "secret_2");
}

TEST_F(CloudProtocolCertificates, IssuedUnitCerts)
{
    constexpr auto cJSON = R"({
        "messageType": "issuedUnitCertificates",
        "certificates": [
            {
                "type": "iam",
                "node": {
                    "id": "node1"
                },
                "certificateChain": "cert_chain_1"
            },
            {
                "type": "offline",
                "node": {
                    "id": "node2"
                },
                "certificateChain": "cert_chain_2"
            },
            {
                "type": "cm",
                "node": {
                    "id": "node3"
                },
                "certificateChain": ""
            }
        ]
    })";

    auto parsedCertificates = std::make_unique<IssuedUnitCerts>();

    auto [jsonVar, err] = common::utils::ParseJson(cJSON);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    auto wrapper = common::utils::CaseInsensitiveObjectWrapper(jsonVar);

    err = FromJSON(wrapper, *parsedCertificates);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    ASSERT_EQ(parsedCertificates->mCertificates.Size(), 3);

    EXPECT_EQ(parsedCertificates->mCertificates[0].mType, CertTypeEnum::eIAM);
    EXPECT_EQ(parsedCertificates->mCertificates[0].mNodeID, "node1");
    EXPECT_EQ(parsedCertificates->mCertificates[0].mCertificateChain, "cert_chain_1");

    EXPECT_EQ(parsedCertificates->mCertificates[1].mType, CertTypeEnum::eOffline);
    EXPECT_EQ(parsedCertificates->mCertificates[1].mNodeID, "node2");
    EXPECT_EQ(parsedCertificates->mCertificates[1].mCertificateChain, "cert_chain_2");

    EXPECT_EQ(parsedCertificates->mCertificates[2].mType, CertTypeEnum::eCM);
    EXPECT_EQ(parsedCertificates->mCertificates[2].mNodeID, "node3");
    EXPECT_EQ(parsedCertificates->mCertificates[2].mCertificateChain, "");
}

TEST_F(CloudProtocolCertificates, IssueUnitCerts)
{
    constexpr auto cJSON = R"({"messageType":"issueUnitCertificates","requests":[)"
                           R"({"type":"iam","node":{"id":"node1"},"csr":"csr_1"},)"
                           R"({"type":"offline","node":{"id":"node2"},"csr":"csr_2"}]})";

    auto unitCerts = std::make_unique<IssueUnitCerts>();

    unitCerts->mRequests.EmplaceBack();
    unitCerts->mRequests.Back().mType   = CertTypeEnum::eIAM;
    unitCerts->mRequests.Back().mNodeID = "node1";
    unitCerts->mRequests.Back().mCSR    = "csr_1";

    unitCerts->mRequests.EmplaceBack();
    unitCerts->mRequests.Back().mType   = CertTypeEnum::eOffline;
    unitCerts->mRequests.Back().mNodeID = "node2";
    unitCerts->mRequests.Back().mCSR    = "csr_2";

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(*unitCerts, *json);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(common::utils::Stringify(json), cJSON);
}

TEST_F(CloudProtocolCertificates, InstallUnitCertsConfirmation)
{
    constexpr auto cJSON = R"({"messageType":"installUnitCertificatesConfirmation","certificates":[)"
                           R"({"type":"iam","node":{"id":"node1"},"serial":"serial_1",)"
                           R"("errorInfo":{"aosCode":1,"exitCode":0,"message":"error_msg"}},)"
                           R"({"type":"offline","node":{"id":"node2"},"serial":"serial_2"}]})";

    auto certsConfirmation = std::make_unique<InstallUnitCertsConfirmation>();

    certsConfirmation->mCertificates.EmplaceBack();
    certsConfirmation->mCertificates.Back().mType   = CertTypeEnum::eIAM;
    certsConfirmation->mCertificates.Back().mNodeID = "node1";
    certsConfirmation->mCertificates.Back().mSerial = "serial_1";
    certsConfirmation->mCertificates.Back().mError  = Error(ErrorEnum::eFailed, "error_msg");

    certsConfirmation->mCertificates.EmplaceBack();
    certsConfirmation->mCertificates.Back().mType   = CertTypeEnum::eOffline;
    certsConfirmation->mCertificates.Back().mNodeID = "node2";
    certsConfirmation->mCertificates.Back().mSerial = "serial_2";

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(*certsConfirmation, *json);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(common::utils::Stringify(json), cJSON);
}

} // namespace aos::cm::communication::cloudprotocol
