/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <common/iamclient/nodesservice.hpp>
#include <common/utils/exception.hpp>
#include <core/common/tests/utils/log.hpp>

#include "mocks/tlscredentialsmock.hpp"
#include "stubs/iamnodesservicestub.hpp"

using namespace testing;
using namespace aos::common::iamclient;

/***********************************************************************************************************************
 * Test Suite
 **********************************************************************************************************************/

class NodesServiceTest : public Test {
protected:
    void SetUp() override
    {
        aos::tests::utils::InitLog();

        mStub = std::make_unique<IAMNodesServiceStub>();

        EXPECT_CALL(mTLSCredentialsMock, GetMTLSClientCredentials(_))
            .WillRepeatedly(Return(aos::RetWithError<std::shared_ptr<grpc::ChannelCredentials>> {
                grpc::InsecureChannelCredentials(), aos::ErrorEnum::eNone}));

        mService = std::make_unique<NodesService>();

        auto err = mService->Init("localhost:8010", "testStorage", mTLSCredentialsMock, true);
        ASSERT_EQ(err, aos::ErrorEnum::eNone);
    }

    void TearDown() override
    {
        mService.reset();
        mStub.reset();
    }

    std::unique_ptr<IAMNodesServiceStub> mStub;
    std::unique_ptr<NodesService>        mService;
    TLSCredentialsMock                   mTLSCredentialsMock;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(NodesServiceTest, PauseNode)
{
    auto err = mService->PauseNode("node1");

    EXPECT_EQ(err, aos::ErrorEnum::eNone);
    EXPECT_STREQ(mStub->GetLastNodeID().c_str(), "node1");
}

TEST_F(NodesServiceTest, PauseNodeWithError)
{
    mStub->SetError(1, "Pause failed");

    auto err = mService->PauseNode("node1");

    EXPECT_NE(err, aos::ErrorEnum::eNone);
    EXPECT_EQ(err.Errno(), 1);
    EXPECT_STREQ(err.Message(), "Pause failed");
}

TEST_F(NodesServiceTest, ResumeNode)
{
    auto err = mService->ResumeNode("node2");

    EXPECT_EQ(err, aos::ErrorEnum::eNone);
    EXPECT_STREQ(mStub->GetLastNodeID().c_str(), "node2");
}

TEST_F(NodesServiceTest, ResumeNodeWithError)
{
    mStub->SetError(2, "Resume failed");

    auto err = mService->ResumeNode("node2");

    EXPECT_NE(err, aos::ErrorEnum::eNone);
    EXPECT_EQ(err.Errno(), 2);
    EXPECT_STREQ(err.Message(), "Resume failed");
}

TEST_F(NodesServiceTest, Reconnect)
{
    auto err = mService->Reconnect();
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    err = mService->PauseNode("node3");
    EXPECT_EQ(err, aos::ErrorEnum::eNone);

    EXPECT_STREQ(mStub->GetLastNodeID().c_str(), "node3");
}
