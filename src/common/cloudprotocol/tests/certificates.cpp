/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>

#include <common/cloudprotocol/certificates.hpp>
#include <common/utils/json.hpp>

using namespace testing;

namespace aos::common::cloudprotocol {

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

TEST_F(CloudProtocolCertificates, EmptyIssuedUnitCerts)
{
    auto certificates = std::make_unique<aos::cloudprotocol::IssuedUnitCerts>();

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(*certificates, *json);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    auto wrapper = utils::CaseInsensitiveObjectWrapper(json);

    EXPECT_EQ(wrapper.GetValue<std::string>("messageType"), "issuedUnitCertificates");
    EXPECT_TRUE(wrapper.Has("certificates"));

    auto unparsedCertificates = std::make_unique<aos::cloudprotocol::IssuedUnitCerts>();

    err = FromJSON(wrapper, *unparsedCertificates);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(*certificates, *unparsedCertificates);
}

TEST_F(CloudProtocolCertificates, IssuedUnitCerts)
{
    auto certificates = std::make_unique<aos::cloudprotocol::IssuedUnitCerts>();

    certificates->mCertificates.EmplaceBack();
    certificates->mCertificates.Back().mType             = CertTypeEnum::eIAM;
    certificates->mCertificates.Back().mNodeID           = "node1";
    certificates->mCertificates.Back().mCertificateChain = "cert_chain_1";

    certificates->mCertificates.EmplaceBack();
    certificates->mCertificates.Back().mType             = CertTypeEnum::eOffline;
    certificates->mCertificates.Back().mNodeID           = "node2";
    certificates->mCertificates.Back().mCertificateChain = "cert_chain_2";

    certificates->mCertificates.EmplaceBack();

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(*certificates, *json);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    auto wrapper = utils::CaseInsensitiveObjectWrapper(json);

    EXPECT_EQ(wrapper.GetValue<std::string>("messageType"), "issuedUnitCertificates");
    EXPECT_TRUE(wrapper.Has("certificates"));

    auto unparsedCertificates = std::make_unique<aos::cloudprotocol::IssuedUnitCerts>();

    err = FromJSON(wrapper, *unparsedCertificates);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(*certificates, *unparsedCertificates);
}

TEST_F(CloudProtocolCertificates, EmptyInstallUnitCertsConfirmation)
{
    auto certificates = std::make_unique<aos::cloudprotocol::InstallUnitCertsConfirmation>();

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(*certificates, *json);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    auto wrapper = utils::CaseInsensitiveObjectWrapper(json);

    EXPECT_EQ(wrapper.GetValue<std::string>("messageType"), "installUnitCertificatesConfirmation");
    EXPECT_TRUE(wrapper.Has("certificates"));

    auto unparsedCertificates = std::make_unique<aos::cloudprotocol::InstallUnitCertsConfirmation>();

    err = FromJSON(wrapper, *unparsedCertificates);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(*certificates, *unparsedCertificates);
}

TEST_F(CloudProtocolCertificates, InstallUnitCertsConfirmation)
{
    auto certificates = std::make_unique<aos::cloudprotocol::InstallUnitCertsConfirmation>();

    certificates->mCertificates.EmplaceBack();
    certificates->mCertificates.Back().mType        = CertTypeEnum::eIAM;
    certificates->mCertificates.Back().mNodeID      = "node1";
    certificates->mCertificates.Back().mSerial      = "serial_1";
    certificates->mCertificates.Back().mStatus      = ItemStatusEnum::eInstalled;
    certificates->mCertificates.Back().mDescription = "cert_description_1";

    certificates->mCertificates.EmplaceBack();
    certificates->mCertificates.Back().mType        = CertTypeEnum::eOffline;
    certificates->mCertificates.Back().mNodeID      = "node2";
    certificates->mCertificates.Back().mSerial      = "serial_2";
    certificates->mCertificates.Back().mStatus      = ItemStatusEnum::eError;
    certificates->mCertificates.Back().mDescription = "cert_description_2";

    certificates->mCertificates.EmplaceBack();

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(*certificates, *json);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    auto wrapper = utils::CaseInsensitiveObjectWrapper(json);

    EXPECT_EQ(wrapper.GetValue<std::string>("messageType"), "installUnitCertificatesConfirmation");
    EXPECT_TRUE(wrapper.Has("certificates"));

    auto unparsedCertificates = std::make_unique<aos::cloudprotocol::InstallUnitCertsConfirmation>();

    err = FromJSON(wrapper, *unparsedCertificates);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(*certificates, *unparsedCertificates);
}

TEST_F(CloudProtocolCertificates, EmptyRenewCertsNotification)
{
    auto certificates = std::make_unique<aos::cloudprotocol::RenewCertsNotification>();

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(*certificates, *json);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    auto wrapper = utils::CaseInsensitiveObjectWrapper(json);

    EXPECT_EQ(wrapper.GetValue<std::string>("messageType"), "renewCertificatesNotification");
    EXPECT_TRUE(wrapper.Has("certificates"));
    EXPECT_TRUE(wrapper.Has("unitSecrets"));

    auto unparsedCertificates = std::make_unique<aos::cloudprotocol::RenewCertsNotification>();

    err = FromJSON(wrapper, *unparsedCertificates);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(*certificates, *unparsedCertificates);
}

TEST_F(CloudProtocolCertificates, RenewCertsNotification)
{
    auto certificates = std::make_unique<aos::cloudprotocol::RenewCertsNotification>();

    certificates->mCertificates.EmplaceBack();
    certificates->mCertificates.Back().mType   = CertTypeEnum::eIAM;
    certificates->mCertificates.Back().mNodeID = "node1";
    certificates->mCertificates.Back().mSerial = "serial_1";

    certificates->mCertificates.EmplaceBack();
    certificates->mCertificates.Back().mType   = CertTypeEnum::eOffline;
    certificates->mCertificates.Back().mNodeID = "node2";
    certificates->mCertificates.Back().mSerial = "serial_2";
    certificates->mCertificates.Back().mValidTill.SetValue(Time::Unix(1706702400));

    certificates->mCertificates.EmplaceBack();

    certificates->mUnitSecrets.mVersion = "v1.0.0";
    certificates->mUnitSecrets.mNodes.Set("node1", "secret_1");
    certificates->mUnitSecrets.mNodes.Set("node2", "secret_2");

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(*certificates, *json);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    auto wrapper = utils::CaseInsensitiveObjectWrapper(json);

    EXPECT_EQ(wrapper.GetValue<std::string>("messageType"), "renewCertificatesNotification");
    EXPECT_TRUE(wrapper.Has("certificates"));

    auto unparsedCertificates = std::make_unique<aos::cloudprotocol::RenewCertsNotification>();

    err = FromJSON(wrapper, *unparsedCertificates);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(*certificates, *unparsedCertificates);
}

TEST_F(CloudProtocolCertificates, EmptyIssueUnitCerts)
{
    auto issueUnitCerts = std::make_unique<aos::cloudprotocol::IssueUnitCerts>();

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(*issueUnitCerts, *json);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    auto wrapper = utils::CaseInsensitiveObjectWrapper(json);

    EXPECT_EQ(wrapper.GetValue<std::string>("messageType"), "issueUnitCertificates");
    EXPECT_TRUE(wrapper.Has("requests"));

    auto unparsedIssueUnitCerts = std::make_unique<aos::cloudprotocol::IssueUnitCerts>();

    err = FromJSON(wrapper, *unparsedIssueUnitCerts);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(*issueUnitCerts, *unparsedIssueUnitCerts);
}

TEST_F(CloudProtocolCertificates, IssueUnitCerts)
{
    auto issueUnitCerts = std::make_unique<aos::cloudprotocol::IssueUnitCerts>();

    issueUnitCerts->mRequests.EmplaceBack();
    issueUnitCerts->mRequests.Back().mType   = CertTypeEnum::eIAM;
    issueUnitCerts->mRequests.Back().mNodeID = "node1";
    issueUnitCerts->mRequests.Back().mCsr    = "csr_1";

    issueUnitCerts->mRequests.EmplaceBack();
    issueUnitCerts->mRequests.Back().mType   = CertTypeEnum::eOffline;
    issueUnitCerts->mRequests.Back().mNodeID = "node2";
    issueUnitCerts->mRequests.Back().mCsr    = "csr_2";

    issueUnitCerts->mRequests.EmplaceBack();

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(*issueUnitCerts, *json);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    auto wrapper = utils::CaseInsensitiveObjectWrapper(json);

    EXPECT_EQ(wrapper.GetValue<std::string>("messageType"), "issueUnitCertificates");
    EXPECT_TRUE(wrapper.Has("requests"));

    auto unparsedIssueUnitCerts = std::make_unique<aos::cloudprotocol::IssueUnitCerts>();

    err = FromJSON(wrapper, *unparsedIssueUnitCerts);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(*issueUnitCerts, *unparsedIssueUnitCerts);
}

} // namespace aos::common::cloudprotocol
