/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <common/iamclient/provisioningservice.hpp>
#include <common/utils/exception.hpp>
#include <core/common/tests/utils/log.hpp>

#include "mocks/tlscredentialsmock.hpp"
#include "stubs/iamprovisioningservicestub.hpp"

using namespace testing;
using namespace aos::common::iamclient;

/***********************************************************************************************************************
 * Test Suite
 **********************************************************************************************************************/

class ProvisioningServiceTest : public Test {
protected:
    void SetUp() override
    {
        aos::tests::utils::InitLog();

        mStub = std::make_unique<IAMProvisioningServiceStub>();

        EXPECT_CALL(mTLSCredentialsMock, GetMTLSClientCredentials(_))
            .WillRepeatedly(Return(aos::RetWithError<std::shared_ptr<grpc::ChannelCredentials>> {
                grpc::InsecureChannelCredentials(), aos::ErrorEnum::eNone}));

        mService = std::make_unique<ProvisioningService>();

        auto err = mService->Init("localhost:8008", "testStorage", mTLSCredentialsMock, true);
        ASSERT_EQ(err, aos::ErrorEnum::eNone);
    }

    void TearDown() override
    {
        mService.reset();
        mStub.reset();
    }

    std::unique_ptr<IAMProvisioningServiceStub> mStub;
    std::unique_ptr<ProvisioningService>        mService;
    TLSCredentialsMock                          mTLSCredentialsMock;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(ProvisioningServiceTest, GetCertTypes)
{
    mStub->SetCertTypes({"online", "offline", "iam"});

    aos::StaticArray<aos::StaticString<aos::cCertTypeLen>, 10> certTypes;

    auto err = mService->GetCertTypes("node1", certTypes);

    EXPECT_EQ(err, aos::ErrorEnum::eNone);
    EXPECT_EQ(certTypes.Size(), 3);
    EXPECT_STREQ(certTypes[0].CStr(), "online");
    EXPECT_STREQ(certTypes[1].CStr(), "offline");
    EXPECT_STREQ(certTypes[2].CStr(), "iam");
    EXPECT_STREQ(mStub->GetLastNodeID().c_str(), "node1");
}

TEST_F(ProvisioningServiceTest, StartProvisioning)
{
    auto err = mService->StartProvisioning("node1", "password123");

    EXPECT_EQ(err, aos::ErrorEnum::eNone);
    EXPECT_STREQ(mStub->GetLastNodeID().c_str(), "node1");
    EXPECT_STREQ(mStub->GetLastPassword().c_str(), "password123");
}

TEST_F(ProvisioningServiceTest, StartProvisioningWithError)
{
    mStub->SetProvisioningError(1, "Provisioning failed");

    auto err = mService->StartProvisioning("node1", "password123");

    EXPECT_NE(err, aos::ErrorEnum::eNone);
    EXPECT_EQ(err.Errno(), 1);
    EXPECT_STREQ(err.Message(), "Provisioning failed");
}

TEST_F(ProvisioningServiceTest, FinishProvisioning)
{
    auto err = mService->FinishProvisioning("node2", "password456");

    EXPECT_EQ(err, aos::ErrorEnum::eNone);
    EXPECT_STREQ(mStub->GetLastNodeID().c_str(), "node2");
    EXPECT_STREQ(mStub->GetLastPassword().c_str(), "password456");
}

TEST_F(ProvisioningServiceTest, FinishProvisioningWithError)
{
    mStub->SetProvisioningError(2, "Finish failed");

    auto err = mService->FinishProvisioning("node2", "password456");

    EXPECT_NE(err, aos::ErrorEnum::eNone);
    EXPECT_EQ(err.Errno(), 2);
    EXPECT_STREQ(err.Message(), "Finish failed");
}

TEST_F(ProvisioningServiceTest, Deprovision)
{
    auto err = mService->Deprovision("node3", "password789");

    EXPECT_EQ(err, aos::ErrorEnum::eNone);
    EXPECT_STREQ(mStub->GetLastNodeID().c_str(), "node3");
    EXPECT_STREQ(mStub->GetLastPassword().c_str(), "password789");
}

TEST_F(ProvisioningServiceTest, DeprovisionWithError)
{
    mStub->SetProvisioningError(3, "Deprovision failed");

    auto err = mService->Deprovision("node3", "password789");

    EXPECT_NE(err, aos::ErrorEnum::eNone);
    EXPECT_EQ(err.Errno(), 3);
    EXPECT_STREQ(err.Message(), "Deprovision failed");
}

TEST_F(ProvisioningServiceTest, Reconnect)
{
    auto err = mService->Reconnect();
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    mStub->SetCertTypes({"online", "offline"});

    aos::StaticArray<aos::StaticString<aos::cCertTypeLen>, 10> certTypes;

    err = mService->GetCertTypes("node4", certTypes);

    EXPECT_EQ(err, aos::ErrorEnum::eNone);
    EXPECT_EQ(certTypes.Size(), 2);
    EXPECT_STREQ(certTypes[0].CStr(), "online");
    EXPECT_STREQ(certTypes[1].CStr(), "offline");
    EXPECT_STREQ(mStub->GetLastNodeID().c_str(), "node4");
}
