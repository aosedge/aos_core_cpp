/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <common/iamclient/publiccurrentnodeservice.hpp>
#include <common/utils/exception.hpp>
#include <core/common/tests/utils/log.hpp>

#include "mocks/nodeinfolistenermock.hpp"
#include "mocks/tlscredentialsmock.hpp"
#include "stubs/iampubliccurrentnodeservicestub.hpp"

using namespace testing;
using namespace aos::common::iamclient;

/***********************************************************************************************************************
 * Test Suite
 **********************************************************************************************************************/

class PublicCurrentNodeServiceTest : public Test {
protected:
    void SetUp() override
    {
        aos::tests::utils::InitLog();

        mStub = std::make_unique<IAMPublicCurrentNodeServiceStub>();

        EXPECT_CALL(mTLSCredentialsMock, GetTLSClientCredentials(_))
            .WillRepeatedly(Return(aos::RetWithError<std::shared_ptr<grpc::ChannelCredentials>> {
                grpc::InsecureChannelCredentials(), aos::ErrorEnum::eNone}));

        mService = std::make_unique<PublicCurrentNodeService>();

        auto err = mService->Init("localhost:8005", mTLSCredentialsMock, true);
        ASSERT_EQ(err, aos::ErrorEnum::eNone);
    }

    void TearDown() override
    {
        mService.reset();
        mStub.reset();
    }

    std::unique_ptr<IAMPublicCurrentNodeServiceStub> mStub;
    std::unique_ptr<PublicCurrentNodeService>        mService;
    TLSCredentialsMock                               mTLSCredentialsMock;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(PublicCurrentNodeServiceTest, GetCurrentNodeInfo)
{
    mStub->SetNodeInfo("node1", "main");

    aos::NodeInfo nodeInfo;

    auto err = mService->GetCurrentNodeInfo(nodeInfo);

    EXPECT_EQ(err, aos::ErrorEnum::eNone);
    EXPECT_STREQ(nodeInfo.mNodeID.CStr(), "node1");
    EXPECT_STREQ(nodeInfo.mNodeType.CStr(), "main");
}

TEST_F(PublicCurrentNodeServiceTest, SubscribeNodeInfoChanged)
{
    NodeInfoListenerMock listener;

    auto err = mService->SubscribeListener(listener);
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    ASSERT_TRUE(mStub->WaitForConnection());

    EXPECT_CALL(listener, OnCurrentNodeInfoChanged(_)).WillOnce(Invoke([](const aos::NodeInfo& nodeInfo) {
        EXPECT_STREQ(nodeInfo.mNodeID.CStr(), "node2");
        EXPECT_STREQ(nodeInfo.mNodeType.CStr(), "secondary");
    }));

    ASSERT_TRUE(mStub->SendNodeInfoChanged("node2", "secondary"));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    err = mService->UnsubscribeListener(listener);
    EXPECT_EQ(err, aos::ErrorEnum::eNone);
}

TEST_F(PublicCurrentNodeServiceTest, SubscribeMultipleListeners)
{
    NodeInfoListenerMock listener1;
    NodeInfoListenerMock listener2;

    auto err = mService->SubscribeListener(listener1);
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    err = mService->SubscribeListener(listener2);
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    ASSERT_TRUE(mStub->WaitForConnection());

    EXPECT_CALL(listener1, OnCurrentNodeInfoChanged(_)).Times(1);
    EXPECT_CALL(listener2, OnCurrentNodeInfoChanged(_)).Times(1);

    ASSERT_TRUE(mStub->SendNodeInfoChanged("node1", "main"));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    err = mService->UnsubscribeListener(listener1);
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    EXPECT_CALL(listener1, OnCurrentNodeInfoChanged(_)).Times(0);
    EXPECT_CALL(listener2, OnCurrentNodeInfoChanged(_)).Times(1);

    ASSERT_TRUE(mStub->SendNodeInfoChanged("node2", "secondary"));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    err = mService->UnsubscribeListener(listener2);
    EXPECT_EQ(err, aos::ErrorEnum::eNone);
}

TEST_F(PublicCurrentNodeServiceTest, Reconnect)
{
    NodeInfoListenerMock listener;

    auto err = mService->SubscribeListener(listener);
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    ASSERT_TRUE(mStub->WaitForConnection());

    EXPECT_CALL(listener, OnCurrentNodeInfoChanged(_)).WillOnce(Invoke([](const aos::NodeInfo& nodeInfo) {
        EXPECT_STREQ(nodeInfo.mNodeID.CStr(), "node_before");
        EXPECT_STREQ(nodeInfo.mNodeType.CStr(), "type_before");
    }));

    ASSERT_TRUE(mStub->SendNodeInfoChanged("node_before", "type_before"));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    err = mService->Reconnect();
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ASSERT_TRUE(mStub->WaitForConnection());

    EXPECT_CALL(listener, OnCurrentNodeInfoChanged(_)).WillOnce(Invoke([](const aos::NodeInfo& nodeInfo) {
        EXPECT_STREQ(nodeInfo.mNodeID.CStr(), "node_after");
        EXPECT_STREQ(nodeInfo.mNodeType.CStr(), "type_after");
    }));

    ASSERT_TRUE(mStub->SendNodeInfoChanged("node_after", "type_after"));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    err = mService->UnsubscribeListener(listener);
    EXPECT_EQ(err, aos::ErrorEnum::eNone);
}
