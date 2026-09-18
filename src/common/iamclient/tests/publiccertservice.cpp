/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <common/iamclient/publiccertservice.hpp>
#include <common/utils/exception.hpp>
#include <core/common/tests/mocks/certprovidermock.hpp>
#include <core/common/tests/utils/log.hpp>

#include "mocks/tlscredentialsmock.hpp"
#include "stubs/iampubliccertservicestub.hpp"

using namespace testing;
using namespace aos::common::iamclient;
using namespace aos::iamclient;

/***********************************************************************************************************************
 * Test Suite
 **********************************************************************************************************************/

class PublicCertServiceTest : public Test {
protected:
    void SetUp() override
    {
        aos::tests::utils::InitLog();

        mStub = std::make_unique<IAMPublicCertServiceStub>();

        EXPECT_CALL(mTLSCredentialsMock, GetTLSClientCredentials(_))
            .WillRepeatedly(Return(aos::RetWithError<std::shared_ptr<grpc::ChannelCredentials>> {
                grpc::InsecureChannelCredentials(), aos::ErrorEnum::eNone}));

        mService = std::make_unique<PublicCertService>();

        auto err = mService->Init("localhost:8003", mTLSCredentialsMock, true);
        ASSERT_EQ(err, aos::ErrorEnum::eNone);
    }

    void TearDown() override
    {
        mService.reset();
        mStub.reset();
    }

    std::unique_ptr<IAMPublicCertServiceStub> mStub;
    std::unique_ptr<PublicCertService>        mService;
    TLSCredentialsMock                        mTLSCredentialsMock;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(PublicCertServiceTest, GetCert)
{
    mStub->SetCertInfo("test_cert.pem", "test_key.pem");

    aos::CertInfo                                           certInfo;
    aos::StaticArray<uint8_t, aos::crypto::cCertIssuerSize> issuer;
    aos::StaticArray<uint8_t, aos::crypto::cSerialNumSize>  serial;

    auto err = mService->GetCert("online", issuer, serial, certInfo);

    EXPECT_EQ(err, aos::ErrorEnum::eNone);
    EXPECT_STREQ(certInfo.mCertURL.CStr(), "test_cert.pem");
    EXPECT_STREQ(certInfo.mKeyURL.CStr(), "test_key.pem");
    EXPECT_STREQ(mStub->GetRequestedCertType().c_str(), "online");
}

TEST_F(PublicCertServiceTest, SubscribeCertChanged)
{
    CertListenerMock listener;

    auto err = mService->SubscribeListener("online", listener);
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    ASSERT_TRUE(mStub->WaitForConnection("online"));

    EXPECT_CALL(listener, OnCertChanged(_)).WillOnce(Invoke([](const aos::CertInfo& certInfo) {
        EXPECT_STREQ(certInfo.mCertURL.CStr(), "updated_cert.pem");
        EXPECT_STREQ(certInfo.mKeyURL.CStr(), "updated_key.pem");
    }));

    ASSERT_TRUE(mStub->SendCertChanged("online", "updated_cert.pem", "updated_key.pem"));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::cerr << "unsibscribe before" << std::endl;
    err = mService->UnsubscribeListener(listener);
    EXPECT_EQ(err, aos::ErrorEnum::eNone);
    std::cerr << "unsibscribe after" << std::endl;
}

TEST_F(PublicCertServiceTest, SubscribeMultipleListeners)
{
    CertListenerMock listener1;
    CertListenerMock listener2;

    auto err = mService->SubscribeListener("online", listener1);
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    err = mService->SubscribeListener("online", listener2);
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    ASSERT_TRUE(mStub->WaitForConnection("online"));

    EXPECT_CALL(listener1, OnCertChanged(_)).Times(1);
    EXPECT_CALL(listener2, OnCertChanged(_)).Times(1);

    ASSERT_TRUE(mStub->SendCertChanged("online", "cert.pem", "key.pem"));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    err = mService->UnsubscribeListener(listener1);
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    EXPECT_CALL(listener1, OnCertChanged(_)).Times(0);
    EXPECT_CALL(listener2, OnCertChanged(_)).Times(1);

    ASSERT_TRUE(mStub->SendCertChanged("online", "cert2.pem", "key2.pem"));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    err = mService->UnsubscribeListener(listener2);
    EXPECT_EQ(err, aos::ErrorEnum::eNone);
}

TEST_F(PublicCertServiceTest, SubscribeDifferentCertTypes)
{
    CertListenerMock onlineListener;
    CertListenerMock offlineListener;

    auto err = mService->SubscribeListener("online", onlineListener);
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    err = mService->SubscribeListener("offline", offlineListener);
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    ASSERT_TRUE(mStub->WaitForConnection("online"));
    ASSERT_TRUE(mStub->WaitForConnection("offline"));

    EXPECT_CALL(onlineListener, OnCertChanged(_)).Times(1);
    EXPECT_CALL(offlineListener, OnCertChanged(_)).Times(0);

    ASSERT_TRUE(mStub->SendCertChanged("online", "online_cert.pem", "online_key.pem"));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    err = mService->UnsubscribeListener(onlineListener);
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    err = mService->UnsubscribeListener(offlineListener);
    EXPECT_EQ(err, aos::ErrorEnum::eNone);
}

TEST_F(PublicCertServiceTest, Reconnect)
{
    CertListenerMock listener;

    auto err = mService->SubscribeListener("online", listener);
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    ASSERT_TRUE(mStub->WaitForConnection("online"));

    EXPECT_CALL(listener, OnCertChanged(_)).WillOnce(Invoke([](const aos::CertInfo& certInfo) {
        EXPECT_STREQ(certInfo.mCertURL.CStr(), "before_reconnect.pem");
        EXPECT_STREQ(certInfo.mKeyURL.CStr(), "before_key.pem");
    }));

    ASSERT_TRUE(mStub->SendCertChanged("online", "before_reconnect.pem", "before_key.pem"));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    err = mService->Reconnect();
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    // Wait for subscription to re-establish after reconnect
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    ASSERT_TRUE(mStub->WaitForConnection("online"));

    EXPECT_CALL(listener, OnCertChanged(_)).WillOnce(Invoke([](const aos::CertInfo& certInfo) {
        EXPECT_STREQ(certInfo.mCertURL.CStr(), "after_reconnect.pem");
        EXPECT_STREQ(certInfo.mKeyURL.CStr(), "after_key.pem");
    }));

    ASSERT_TRUE(mStub->SendCertChanged("online", "after_reconnect.pem", "after_key.pem"));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    err = mService->UnsubscribeListener(listener);
    EXPECT_EQ(err, aos::ErrorEnum::eNone);
}
