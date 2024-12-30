/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <optional>

#include <gtest/gtest.h>

#include <aos/test/log.hpp>

#include "iamclient/publicservicehandler.hpp"
#include "mocks/certsubscriber.hpp"
#include "stubs/iamserver.hpp"

using namespace testing;
using namespace aos::common::iamclient;

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class IamClientTest : public Test {
public:
    IamClientTest() { mConfig.mIAMPublicServerURL = "localhost:8002"; }

protected:
    void SetUp() override
    {
        aos::InitLog();

        mIAMServerStub.emplace();
        mClient.emplace();

        auto getMTLSCredentials = [this](const aos::iam::certhandler::CertInfo& certInfo, const aos::String&,
                                      aos::crypto::CertLoaderItf&, aos::crypto::x509::ProviderItf&) {
            mCertInfo = certInfo;

            return nullptr;
        };

        auto err = mClient->Init(mConfig, *mCertLoader, *mCryptoProvider, true, std::move(getMTLSCredentials));

        ASSERT_EQ(err, aos::ErrorEnum::eNone);
    }

    std::optional<TestIAMServer> mIAMServerStub;

    std::optional<PublicServiceHandler> mClient;
    aos::crypto::CertLoaderItf*         mCertLoader {};
    aos::crypto::x509::ProviderItf*     mCryptoProvider {};
    Config                              mConfig {};
    aos::iam::certhandler::CertInfo     mCertInfo {};
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(IamClientTest, GetClientMTLSConfig)
{
    aos::iam::certhandler::CertInfo certInfo;
    certInfo.mCertURL = "client_cert";
    certInfo.mKeyURL  = "client_key";

    mIAMServerStub->SetCertInfo(certInfo);

    auto [_, err] = mClient->GetMTLSConfig("client_cert_type");

    EXPECT_EQ(err, aos::ErrorEnum::eNone);
    EXPECT_EQ(mIAMServerStub->GetCertType(), "client_cert_type");
    EXPECT_EQ(mCertInfo, certInfo);
}

TEST_F(IamClientTest, GetCertificate)
{
    aos::iam::certhandler::CertInfo certInfo;
    certInfo.mCertURL = "client_cert";
    certInfo.mKeyURL  = "client_key";

    mIAMServerStub->SetCertInfo(certInfo);

    aos::iam::certhandler::CertInfo requestCertInfo;

    auto err = mClient->GetCertificate("client_cert_type", requestCertInfo);

    EXPECT_EQ(err, aos::ErrorEnum::eNone);
    EXPECT_EQ(mIAMServerStub->GetCertType(), "client_cert_type");
    EXPECT_EQ(certInfo, requestCertInfo);
}

TEST_F(IamClientTest, SubscribeCertChangedAndGetCertificate_MultiSubscription)
{
    MockCertSubscriber subscriber1;
    MockCertSubscriber subscriber2;

    iamanager::v5::CertInfo certInfo;
    certInfo.set_type("client_cert_type");
    certInfo.set_cert_url("client_cert");
    certInfo.set_key_url("client_key");

    mClient->SubscribeCertChanged("client_cert_type", subscriber1);
    mClient->SubscribeCertChanged("client_cert_type", subscriber2);

    EXPECT_TRUE(mIAMServerStub->WaitForConnection());

    mIAMServerStub->SendCertChangedInfo(certInfo);

    EXPECT_CALL(subscriber1, OnCertChanged(_)).Times(1);
    EXPECT_CALL(subscriber2, OnCertChanged(_)).Times(1);

    aos::iam::certhandler::CertInfo requestCertInfo;

    auto err = mClient->GetCertificate("client_cert_type", requestCertInfo);

    EXPECT_EQ(err, aos::ErrorEnum::eNone);
    EXPECT_EQ(requestCertInfo.mCertURL, "client_cert");
    EXPECT_EQ(requestCertInfo.mKeyURL, "client_key");

    mClient->UnsubscribeCertChanged("client_cert_type", subscriber1);

    mIAMServerStub->SendCertChangedInfo(certInfo);

    EXPECT_CALL(subscriber1, OnCertChanged(_)).Times(0);
    EXPECT_CALL(subscriber2, OnCertChanged(_)).Times(1);

    err = mClient->GetCertificate("client_cert_type", requestCertInfo);

    EXPECT_EQ(err, aos::ErrorEnum::eNone);
    EXPECT_EQ(requestCertInfo.mCertURL, "client_cert");
    EXPECT_EQ(requestCertInfo.mKeyURL, "client_key");

    mClient->UnsubscribeCertChanged("client_cert_type", subscriber2);
    mClient->Close();
    mIAMServerStub->Close();
}
