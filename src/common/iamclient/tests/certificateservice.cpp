/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <common/iamclient/certificateservice.hpp>
#include <common/utils/exception.hpp>
#include <core/common/tests/utils/log.hpp>

#include "mocks/tlscredentialsmock.hpp"
#include "stubs/iamcertificateservicestub.hpp"

using namespace testing;
using namespace aos::common::iamclient;

/***********************************************************************************************************************
 * Test Suite
 **********************************************************************************************************************/

class CertificateServiceTest : public Test {
protected:
    void SetUp() override
    {
        aos::tests::utils::InitLog();

        mStub = std::make_unique<IAMCertificateServiceStub>();

        EXPECT_CALL(mTLSCredentialsMock, GetMTLSClientCredentials(_))
            .WillRepeatedly(Return(aos::RetWithError<std::shared_ptr<grpc::ChannelCredentials>> {
                grpc::InsecureChannelCredentials(), aos::ErrorEnum::eNone}));

        mService = std::make_unique<CertificateService>();

        auto err = mService->Init("localhost:8009", "testStorage", mTLSCredentialsMock, true);
        ASSERT_EQ(err, aos::ErrorEnum::eNone);
    }

    void TearDown() override
    {
        mService.reset();
        mStub.reset();
    }

    std::unique_ptr<IAMCertificateServiceStub> mStub;
    std::unique_ptr<CertificateService>        mService;
    TLSCredentialsMock                         mTLSCredentialsMock;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(CertificateServiceTest, CreateKey)
{
    mStub->SetCSR("-----BEGIN CERTIFICATE REQUEST-----\ntest_csr\n-----END CERTIFICATE REQUEST-----");

    aos::StaticString<1024> csr;

    auto err = mService->CreateKey("node1", "online", "CN=test", "password123", csr);

    EXPECT_EQ(err, aos::ErrorEnum::eNone);
    EXPECT_STREQ(csr.CStr(), "-----BEGIN CERTIFICATE REQUEST-----\ntest_csr\n-----END CERTIFICATE REQUEST-----");
    EXPECT_STREQ(mStub->GetLastNodeID().c_str(), "node1");
    EXPECT_STREQ(mStub->GetLastCertType().c_str(), "online");
    EXPECT_STREQ(mStub->GetLastSubject().c_str(), "CN=test");
    EXPECT_STREQ(mStub->GetLastPassword().c_str(), "password123");
}

TEST_F(CertificateServiceTest, CreateKeyWithError)
{
    mStub->SetError(1, "Key creation failed");

    aos::StaticString<1024> csr;

    auto err = mService->CreateKey("node1", "online", "CN=test", "password123", csr);

    EXPECT_NE(err, aos::ErrorEnum::eNone);
    EXPECT_EQ(err.Errno(), 1);
    EXPECT_STREQ(err.Message(), "Key creation failed");
}

TEST_F(CertificateServiceTest, ApplyCert)
{
    mStub->SetCertInfo("file:///path/to/cert.pem", "file:///path/to/key.pem");

    aos::CertInfo certInfo;

    auto err = mService->ApplyCert(
        "node2", "offline", "-----BEGIN CERTIFICATE-----\ntest_cert\n-----END CERTIFICATE-----", certInfo);

    EXPECT_EQ(err, aos::ErrorEnum::eNone);
    EXPECT_STREQ(certInfo.mCertURL.CStr(), "file:///path/to/cert.pem");
    EXPECT_STREQ(certInfo.mKeyURL.CStr(), "file:///path/to/key.pem");
    EXPECT_STREQ(mStub->GetLastNodeID().c_str(), "node2");
    EXPECT_STREQ(mStub->GetLastCertType().c_str(), "offline");
    EXPECT_STREQ(mStub->GetLastPemCert().c_str(), "-----BEGIN CERTIFICATE-----\ntest_cert\n-----END CERTIFICATE-----");
}

TEST_F(CertificateServiceTest, ApplyCertWithError)
{
    mStub->SetError(2, "Certificate application failed");

    aos::CertInfo certInfo;

    auto err = mService->ApplyCert(
        "node2", "offline", "-----BEGIN CERTIFICATE-----\ntest_cert\n-----END CERTIFICATE-----", certInfo);

    EXPECT_NE(err, aos::ErrorEnum::eNone);
    EXPECT_EQ(err.Errno(), 2);
    EXPECT_STREQ(err.Message(), "Certificate application failed");
}

TEST_F(CertificateServiceTest, Reconnect)
{
    auto err = mService->Reconnect();
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    mStub->SetCSR("-----BEGIN CERTIFICATE REQUEST-----\nreconnect_test\n-----END CERTIFICATE REQUEST-----");

    aos::StaticString<1024> csr;
    err = mService->CreateKey("node3", "online", "CN=reconnect", "pass456", csr);

    EXPECT_EQ(err, aos::ErrorEnum::eNone);
    EXPECT_STREQ(csr.CStr(), "-----BEGIN CERTIFICATE REQUEST-----\nreconnect_test\n-----END CERTIFICATE REQUEST-----");
}
