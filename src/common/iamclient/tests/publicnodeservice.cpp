/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <common/iamclient/publicnodeservice.hpp>
#include <common/utils/exception.hpp>
#include <core/common/tests/utils/log.hpp>

#include "mocks/nodeslistenermock.hpp"
#include "mocks/tlscredentialsmock.hpp"
#include "stubs/iampublicnodesservicestub.hpp"

using namespace testing;
using namespace aos::common::iamclient;

/***********************************************************************************************************************
 * Test Suite
 **********************************************************************************************************************/

class PublicNodesServiceTest : public Test {
protected:
    void SetUp() override
    {
        aos::tests::utils::InitLog();

        mStub = std::make_unique<IAMPublicNodesServiceStub>();

        EXPECT_CALL(mTLSCredentialsMock, GetTLSClientCredentials(_))
            .WillRepeatedly(Return(aos::RetWithError<std::shared_ptr<grpc::ChannelCredentials>> {
                grpc::InsecureChannelCredentials(), aos::ErrorEnum::eNone}));

        mService = std::make_unique<PublicNodesService>();

        auto err = mService->Init("localhost:8007", mTLSCredentialsMock, true);
        ASSERT_EQ(err, aos::ErrorEnum::eNone);
    }

    void TearDown() override
    {
        mService.reset();
        mStub.reset();
    }

    std::unique_ptr<IAMPublicNodesServiceStub> mStub;
    std::unique_ptr<PublicNodesService>        mService;
    TLSCredentialsMock                         mTLSCredentialsMock;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(PublicNodesServiceTest, GetAllNodeIDs)
{
    mStub->SetNodeIds({"node1", "node2", "node3"});

    aos::StaticArray<aos::StaticString<aos::cIDLen>, 10> nodeIds;

    auto err = mService->GetAllNodeIDs(nodeIds);

    EXPECT_EQ(err, aos::ErrorEnum::eNone);
    EXPECT_EQ(nodeIds.Size(), 3);
    EXPECT_STREQ(nodeIds[0].CStr(), "node1");
    EXPECT_STREQ(nodeIds[1].CStr(), "node2");
    EXPECT_STREQ(nodeIds[2].CStr(), "node3");
}

TEST_F(PublicNodesServiceTest, GetNodeInfo)
{
    mStub->SetNodeInfo("node1", "main");
    mStub->SetNodeInfo("node2", "secondary");

    aos::NodeInfo nodeInfo;

    auto err = mService->GetNodeInfo("node1", nodeInfo);

    EXPECT_EQ(err, aos::ErrorEnum::eNone);
    EXPECT_STREQ(nodeInfo.mNodeID.CStr(), "node1");
    EXPECT_STREQ(nodeInfo.mNodeType.CStr(), "main");

    err = mService->GetNodeInfo("node2", nodeInfo);

    EXPECT_EQ(err, aos::ErrorEnum::eNone);
    EXPECT_STREQ(nodeInfo.mNodeID.CStr(), "node2");
    EXPECT_STREQ(nodeInfo.mNodeType.CStr(), "secondary");
}

TEST_F(PublicNodesServiceTest, SubscribeNodeChanged)
{
    NodesListenerMock listener;

    auto err = mService->SubscribeListener(listener);
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    ASSERT_TRUE(mStub->WaitForConnection());

    EXPECT_CALL(listener, OnNodeInfoChanged(_)).WillOnce(Invoke([](const aos::NodeInfo& nodeInfo) {
        EXPECT_STREQ(nodeInfo.mNodeID.CStr(), "node1");
        EXPECT_STREQ(nodeInfo.mNodeType.CStr(), "main");
    }));

    ASSERT_TRUE(mStub->SendNodeInfoChanged("node1", "main"));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    err = mService->UnsubscribeListener(listener);
    EXPECT_EQ(err, aos::ErrorEnum::eNone);
}

TEST_F(PublicNodesServiceTest, SubscribeMultipleListeners)
{
    NodesListenerMock listener1;
    NodesListenerMock listener2;

    auto err = mService->SubscribeListener(listener1);
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    err = mService->SubscribeListener(listener2);
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    ASSERT_TRUE(mStub->WaitForConnection());

    EXPECT_CALL(listener1, OnNodeInfoChanged(_)).Times(1);
    EXPECT_CALL(listener2, OnNodeInfoChanged(_)).Times(1);

    ASSERT_TRUE(mStub->SendNodeInfoChanged("node1", "main"));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    err = mService->UnsubscribeListener(listener1);
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    EXPECT_CALL(listener1, OnNodeInfoChanged(_)).Times(0);
    EXPECT_CALL(listener2, OnNodeInfoChanged(_)).Times(1);

    ASSERT_TRUE(mStub->SendNodeInfoChanged("node2", "secondary"));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    err = mService->UnsubscribeListener(listener2);
    EXPECT_EQ(err, aos::ErrorEnum::eNone);
}

TEST_F(PublicNodesServiceTest, Reconnect)
{
    NodesListenerMock listener;

    auto err = mService->SubscribeListener(listener);
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    ASSERT_TRUE(mStub->WaitForConnection());

    EXPECT_CALL(listener, OnNodeInfoChanged(_)).WillOnce(Invoke([](const aos::NodeInfo& nodeInfo) {
        EXPECT_STREQ(nodeInfo.mNodeID.CStr(), "node_before");
        EXPECT_STREQ(nodeInfo.mNodeType.CStr(), "type_before");
    }));

    ASSERT_TRUE(mStub->SendNodeInfoChanged("node_before", "type_before"));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    err = mService->Reconnect();
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ASSERT_TRUE(mStub->WaitForConnection());

    EXPECT_CALL(listener, OnNodeInfoChanged(_)).WillOnce(Invoke([](const aos::NodeInfo& nodeInfo) {
        EXPECT_STREQ(nodeInfo.mNodeID.CStr(), "node_after");
        EXPECT_STREQ(nodeInfo.mNodeType.CStr(), "type_after");
    }));

    ASSERT_TRUE(mStub->SendNodeInfoChanged("node_after", "type_after"));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    err = mService->UnsubscribeListener(listener);
    EXPECT_EQ(err, aos::ErrorEnum::eNone);
}
