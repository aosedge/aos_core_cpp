/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <common/iamclient/permservice.hpp>
#include <common/utils/exception.hpp>
#include <core/common/tests/utils/log.hpp>

#include "mocks/tlscredentialsmock.hpp"
#include "stubs/iampermissionsservicestub.hpp"

using namespace testing;
using namespace aos::common::iamclient;

/***********************************************************************************************************************
 * Test Suite
 **********************************************************************************************************************/

class PermissionsServiceTest : public Test {
protected:
    void SetUp() override
    {
        aos::tests::utils::InitLog();

        mStub = std::make_unique<IAMPermissionsServiceStub>();

        EXPECT_CALL(mTLSCredentialsMock, GetMTLSClientCredentials(_, _))
            .WillRepeatedly(Return(aos::RetWithError<std::shared_ptr<grpc::ChannelCredentials>> {
                grpc::InsecureChannelCredentials(), aos::ErrorEnum::eNone}));

        mService = std::make_unique<PermissionsService>();

        auto err = mService->Init("localhost:8011", "testStorage", mTLSCredentialsMock, true);
        ASSERT_EQ(err, aos::ErrorEnum::eNone);
    }

    void TearDown() override
    {
        mService.reset();
        mStub.reset();
    }

    std::unique_ptr<IAMPermissionsServiceStub> mStub;
    std::unique_ptr<PermissionsService>        mService;
    TLSCredentialsMock                         mTLSCredentialsMock;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(PermissionsServiceTest, RegisterInstance)
{
    mStub->SetSecret("test_secret_12345");

    aos::InstanceIdent instanceIdent;
    instanceIdent.mItemID    = "service1";
    instanceIdent.mSubjectID = "subject1";
    instanceIdent.mInstance  = 42;

    aos::StaticArray<aos::FunctionServicePermissions, 5> permissions;

    auto [secret, err] = mService->RegisterInstance(instanceIdent, permissions);

    EXPECT_EQ(err, aos::ErrorEnum::eNone);
    EXPECT_STREQ(secret.CStr(), "test_secret_12345");
    EXPECT_STREQ(mStub->GetLastItemID().c_str(), "service1");
    EXPECT_STREQ(mStub->GetLastSubjectID().c_str(), "subject1");
    EXPECT_EQ(mStub->GetLastInstance(), 42);
}

TEST_F(PermissionsServiceTest, UnregisterInstance)
{
    aos::InstanceIdent instanceIdent;
    instanceIdent.mItemID    = "service2";
    instanceIdent.mSubjectID = "subject2";
    instanceIdent.mInstance  = 99;

    auto err = mService->UnregisterInstance(instanceIdent);

    EXPECT_EQ(err, aos::ErrorEnum::eNone);
    EXPECT_STREQ(mStub->GetLastItemID().c_str(), "service2");
    EXPECT_STREQ(mStub->GetLastSubjectID().c_str(), "subject2");
    EXPECT_EQ(mStub->GetLastInstance(), 99);
}

TEST_F(PermissionsServiceTest, Reconnect)
{
    auto err = mService->Reconnect();
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    mStub->SetSecret("reconnect_secret_789");

    aos::InstanceIdent instanceIdent;
    instanceIdent.mItemID    = "service3";
    instanceIdent.mSubjectID = "subject3";
    instanceIdent.mInstance  = 123;

    aos::StaticArray<aos::FunctionServicePermissions, 5> permissions;

    auto [secret, err2] = mService->RegisterInstance(instanceIdent, permissions);

    EXPECT_EQ(err2, aos::ErrorEnum::eNone);
    EXPECT_STREQ(secret.CStr(), "reconnect_secret_789");
    EXPECT_STREQ(mStub->GetLastItemID().c_str(), "service3");
    EXPECT_STREQ(mStub->GetLastSubjectID().c_str(), "subject3");
    EXPECT_EQ(mStub->GetLastInstance(), 123);
}
