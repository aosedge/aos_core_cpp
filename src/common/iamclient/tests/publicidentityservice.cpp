/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <common/iamclient/publicidentityservice.hpp>
#include <common/utils/exception.hpp>
#include <core/common/tests/mocks/identprovidermock.hpp>
#include <core/common/tests/utils/log.hpp>

#include "mocks/tlscredentialsmock.hpp"
#include "stubs/iampublicidentityservicestub.hpp"

using namespace testing;
using namespace aos::common::iamclient;
using namespace aos::iamclient;

/***********************************************************************************************************************
 * Test Suite
 **********************************************************************************************************************/

class PublicIdentityServiceTest : public Test {
protected:
    void SetUp() override
    {
        aos::tests::utils::InitLog();

        mStub = std::make_unique<IAMPublicIdentityServiceStub>();

        EXPECT_CALL(mTLSCredentialsMock, GetTLSClientCredentials())
            .WillRepeatedly(Return(aos::RetWithError<std::shared_ptr<grpc::ChannelCredentials>> {
                grpc::InsecureChannelCredentials(), aos::ErrorEnum::eNone}));

        mService = std::make_unique<PublicIdentityService>();

        auto err = mService->Init("localhost:8006", mTLSCredentialsMock, true);
        ASSERT_EQ(err, aos::ErrorEnum::eNone);
    }

    void TearDown() override
    {
        mService.reset();
        mStub.reset();
    }

    std::unique_ptr<IAMPublicIdentityServiceStub> mStub;
    std::unique_ptr<PublicIdentityService>        mService;
    TLSCredentialsMock                            mTLSCredentialsMock;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(PublicIdentityServiceTest, GetSystemInfo)
{
    mStub->SetSystemInfo("system123", "model456");

    aos::SystemInfo info;
    auto            err = mService->GetSystemInfo(info);

    EXPECT_EQ(err, aos::ErrorEnum::eNone);
    EXPECT_STREQ(info.mSystemID.CStr(), "system123");
    EXPECT_STREQ(info.mUnitModel.CStr(), "model456");
}

TEST_F(PublicIdentityServiceTest, GetSubjects)
{
    mStub->SetSubjects({"subject1", "subject2", "subject3"});

    aos::StaticArray<aos::StaticString<aos::cIDLen>, 10> subjects;

    auto err = mService->GetSubjects(subjects);

    EXPECT_EQ(err, aos::ErrorEnum::eNone);
    EXPECT_EQ(subjects.Size(), 3);
    EXPECT_STREQ(subjects[0].CStr(), "subject1");
    EXPECT_STREQ(subjects[1].CStr(), "subject2");
    EXPECT_STREQ(subjects[2].CStr(), "subject3");
}

TEST_F(PublicIdentityServiceTest, SubscribeSubjectsChanged)
{
    SubjectsListenerMock listener;

    auto err = mService->SubscribeListener(listener);
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    ASSERT_TRUE(mStub->WaitForConnection());

    EXPECT_CALL(listener, SubjectsChanged(_)).WillOnce(Invoke([](const auto& subjects) {
        EXPECT_EQ(subjects.Size(), 2);
        EXPECT_STREQ(subjects[0].CStr(), "subjectA");
        EXPECT_STREQ(subjects[1].CStr(), "subjectB");
    }));

    ASSERT_TRUE(mStub->SendSubjectsChanged({"subjectA", "subjectB"}));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    err = mService->UnsubscribeListener(listener);
    EXPECT_EQ(err, aos::ErrorEnum::eNone);
}

TEST_F(PublicIdentityServiceTest, SubscribeMultipleListeners)
{
    SubjectsListenerMock listener1;
    SubjectsListenerMock listener2;

    auto err = mService->SubscribeListener(listener1);
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    err = mService->SubscribeListener(listener2);
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    ASSERT_TRUE(mStub->WaitForConnection());

    EXPECT_CALL(listener1, SubjectsChanged(_)).Times(1);
    EXPECT_CALL(listener2, SubjectsChanged(_)).Times(1);

    ASSERT_TRUE(mStub->SendSubjectsChanged({"subject1", "subject2"}));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    err = mService->UnsubscribeListener(listener1);
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    EXPECT_CALL(listener1, SubjectsChanged(_)).Times(0);
    EXPECT_CALL(listener2, SubjectsChanged(_)).Times(1);

    ASSERT_TRUE(mStub->SendSubjectsChanged({"subject3"}));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    err = mService->UnsubscribeListener(listener2);
    EXPECT_EQ(err, aos::ErrorEnum::eNone);
}

TEST_F(PublicIdentityServiceTest, Reconnect)
{
    SubjectsListenerMock listener;

    auto err = mService->SubscribeListener(listener);
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    ASSERT_TRUE(mStub->WaitForConnection());

    EXPECT_CALL(listener, SubjectsChanged(_)).WillOnce(Invoke([](const auto& subjects) {
        EXPECT_EQ(subjects.Size(), 2);
        EXPECT_STREQ(subjects[0].CStr(), "before1");
        EXPECT_STREQ(subjects[1].CStr(), "before2");
    }));

    ASSERT_TRUE(mStub->SendSubjectsChanged({"before1", "before2"}));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    err = mService->Reconnect();
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ASSERT_TRUE(mStub->WaitForConnection());

    EXPECT_CALL(listener, SubjectsChanged(_)).WillOnce(Invoke([](const auto& subjects) {
        EXPECT_EQ(subjects.Size(), 2);
        EXPECT_STREQ(subjects[0].CStr(), "after1");
        EXPECT_STREQ(subjects[1].CStr(), "after2");
    }));

    ASSERT_TRUE(mStub->SendSubjectsChanged({"after1", "after2"}));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    err = mService->UnsubscribeListener(listener);
    EXPECT_EQ(err, aos::ErrorEnum::eNone);
}
