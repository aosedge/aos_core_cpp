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

        EXPECT_CALL(mTLSCredentialsMock, GetTLSClientCredentials())
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

/***********************************************************************************************************************
 * RegisterNode Tests
 **********************************************************************************************************************/

class PublicNodesServiceStub : public PublicNodesService {
public:
    std::vector<iamanager::v6::IAMIncomingMessages> mReceivedMessages;
    std::mutex                                      mMessagesMutex;
    std::condition_variable                         mMessagesCV;

protected:
    aos::Error ReceiveMessage(const iamanager::v6::IAMIncomingMessages& msg) override
    {
        std::lock_guard lock {mMessagesMutex};

        mReceivedMessages.push_back(msg);
        mMessagesCV.notify_all();

        return aos::ErrorEnum::eNone;
    }

public:
    bool WaitForMessage(std::chrono::seconds timeout = std::chrono::seconds(5))
    {
        std::unique_lock lock {mMessagesMutex};

        return mMessagesCV.wait_for(lock, timeout, [this] { return !mReceivedMessages.empty(); });
    }

    size_t GetReceivedMessagesCount()
    {
        std::lock_guard lock {mMessagesMutex};

        return mReceivedMessages.size();
    }

    iamanager::v6::IAMIncomingMessages GetLastMessage()
    {
        std::lock_guard lock {mMessagesMutex};

        return mReceivedMessages.back();
    }
};

class RegisterNodeTest : public Test {
protected:
    void SetUp() override
    {
        aos::tests::utils::InitLog();

        mStub = std::make_unique<IAMPublicNodesServiceStub>();

        EXPECT_CALL(mTLSCredentialsMock, GetTLSClientCredentials())
            .WillRepeatedly(Return(aos::RetWithError<std::shared_ptr<grpc::ChannelCredentials>> {
                grpc::InsecureChannelCredentials(), aos::ErrorEnum::eNone}));

        mService = std::make_unique<PublicNodesServiceStub>();

        auto err = mService->Init("localhost:8007", mTLSCredentialsMock, true);
        ASSERT_EQ(err, aos::ErrorEnum::eNone);
    }

    void TearDown() override
    {
        mService->Stop();
        mService.reset();
        mStub.reset();
    }

    std::unique_ptr<IAMPublicNodesServiceStub> mStub;
    std::unique_ptr<PublicNodesServiceStub>    mService;
    TLSCredentialsMock                         mTLSCredentialsMock;
};

TEST_F(RegisterNodeTest, StartAndStop)
{
    auto err = mService->Start();
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    ASSERT_TRUE(mStub->WaitForRegisterNodeConnection());

    mService->Stop();
}

TEST_F(RegisterNodeTest, SendMessage)
{
    auto err = mService->Start();
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    ASSERT_TRUE(mStub->WaitForRegisterNodeConnection());

    iamanager::v6::IAMOutgoingMessages outgoingMsg;
    outgoingMsg.mutable_node_info()->set_node_id("test-node");
    outgoingMsg.mutable_node_info()->set_node_type("secondary");

    err = mService->SendMessage(outgoingMsg);
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    iamanager::v6::IAMOutgoingMessages receivedMsg;
    ASSERT_TRUE(mStub->WaitForOutgoingMessage(receivedMsg));

    EXPECT_TRUE(receivedMsg.has_node_info());
    EXPECT_EQ(receivedMsg.node_info().node_id(), "test-node");
    EXPECT_EQ(receivedMsg.node_info().node_type(), "secondary");
}

TEST_F(RegisterNodeTest, ReceiveMessage)
{
    auto err = mService->Start();
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    ASSERT_TRUE(mStub->WaitForRegisterNodeConnection());

    iamanager::v6::IAMIncomingMessages incomingMsg;
    incomingMsg.mutable_start_provisioning_request()->set_node_id("test-node");
    incomingMsg.mutable_start_provisioning_request()->set_password("test-password");

    ASSERT_TRUE(mStub->SendIncomingMessage(incomingMsg));

    ASSERT_TRUE(mService->WaitForMessage());

    EXPECT_EQ(mService->GetReceivedMessagesCount(), 1);

    auto received = mService->GetLastMessage();
    EXPECT_TRUE(received.has_start_provisioning_request());
    EXPECT_EQ(received.start_provisioning_request().node_id(), "test-node");
    EXPECT_EQ(received.start_provisioning_request().password(), "test-password");
}

TEST_F(RegisterNodeTest, SendMessageWhenNotConnected)
{
    iamanager::v6::IAMOutgoingMessages outgoingMsg;
    outgoingMsg.mutable_node_info()->set_node_id("test-node");

    auto err = mService->SendMessage(outgoingMsg);
    EXPECT_EQ(err, aos::ErrorEnum::eCanceled);
}

TEST_F(RegisterNodeTest, MultipleStartCalls)
{
    auto err = mService->Start();
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    err = mService->Start();
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    ASSERT_TRUE(mStub->WaitForRegisterNodeConnection());

    mService->Stop();
}

TEST_F(RegisterNodeTest, StopWithoutStart)
{
    mService->Stop();
}
