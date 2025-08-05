/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <optional>

#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>

#include <iamanager/v5/iamanager.grpc.pb.h>
#include <servicemanager/v4/servicemanager.grpc.pb.h>

#include <mp/communication/communicationmanager.hpp>
#include <mp/communication/socket.hpp>
#include <mp/config/config.hpp>

#include "stubs/transport.hpp"

using namespace testing;

namespace aos::mp::communication {

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class CommunicationOpenManagerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        tests::utils::InitLog();

        mConfig.mIAMConfig.mOpenPort = 8080;
        mConfig.mCMConfig.mOpenPort  = 30001;

        mServer.emplace();
        EXPECT_EQ(mServer->Init(30001), ErrorEnum::eNone);

        mClient.emplace("localhost", 30001);

        mCommManager.emplace();
        mCommManagerClient.emplace(mClient.value());

        mIAMClientChannel = mCommManagerClient->CreateCommChannel(8080);
        mCMClientChannel  = mCommManagerClient->CreateCommChannel(30001);
    }

    void TearDown() override
    {
        mClient->Close();
        mCommManager->Stop();
        mIAMConnection.Stop();
        mCMConnection.Stop();
        mCommManagerClient->Close();
    }

    std::optional<communication::Socket> mServer;
    std::optional<SocketClient>          mClient;

    std::shared_ptr<CommChannelItf> mIAMClientChannel;
    std::shared_ptr<CommChannelItf> mCMClientChannel;
    std::optional<CommManager>      mCommManagerClient;

    config::Config                        mConfig;
    common::iamclient::TLSCredentialsItf* mCertProvider {};
    crypto::CertLoaderItf*                mCertLoader {};
    crypto::x509::ProviderItf*            mCryptoProvider {};
    Handler                               IAMHandler {};
    Handler                               CMHandler {};
    IAMConnection                         mIAMConnection {};
    CMConnection                          mCMConnection {};
    std::optional<CommunicationManager>   mCommManager;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(CommunicationOpenManagerTest, TestOpenIAMChannel)
{
    EXPECT_EQ(mCommManager->Init(mConfig, mServer.value()), ErrorEnum::eNone);

    auto err = mIAMConnection.Init(mConfig.mIAMConfig.mOpenPort, IAMHandler, *mCommManager);
    EXPECT_EQ(err, ErrorEnum::eNone);

    err = mCMConnection.Init(mConfig, CMHandler, *mCommManager);
    EXPECT_EQ(err, ErrorEnum::eNone);

    EXPECT_EQ(mCommManager->Start(), ErrorEnum::eNone);
    EXPECT_EQ(mIAMConnection.Start(), ErrorEnum::eNone);
    EXPECT_EQ(mCMConnection.Start(), ErrorEnum::eNone);

    iamanager::v5::IAMOutgoingMessages outgoingMsg;
    outgoingMsg.mutable_start_provisioning_response();

    std::vector<uint8_t> messageData(outgoingMsg.ByteSizeLong());
    EXPECT_TRUE(outgoingMsg.SerializeToArray(messageData.data(), messageData.size()));

    auto protobufHeader = PrepareProtobufHeader(messageData.size());
    protobufHeader.insert(protobufHeader.end(), messageData.begin(), messageData.end());
    EXPECT_EQ(mIAMClientChannel->Write(protobufHeader), ErrorEnum::eNone);

    auto [receivedMsg, errReceive] = IAMHandler.GetOutgoingMessages();
    EXPECT_EQ(errReceive, ErrorEnum::eNone);
    EXPECT_TRUE(outgoingMsg.ParseFromArray(receivedMsg.data(), receivedMsg.size()));
    EXPECT_TRUE(outgoingMsg.has_start_provisioning_response());

    servicemanager::v4::SMOutgoingMessages smOutgoingMessages;
    smOutgoingMessages.mutable_node_config_status();
    std::vector<uint8_t> messageData2(smOutgoingMessages.ByteSizeLong());
    EXPECT_TRUE(smOutgoingMessages.SerializeToArray(messageData2.data(), messageData2.size()));

    protobufHeader = PrepareProtobufHeader(messageData2.size());
    protobufHeader.insert(protobufHeader.end(), messageData2.begin(), messageData2.end());

    EXPECT_EQ(mCMClientChannel->Write(protobufHeader), ErrorEnum::eNone);

    auto [receivedMsg2, errReceive2] = CMHandler.GetOutgoingMessages();
    EXPECT_EQ(errReceive2, ErrorEnum::eNone);

    EXPECT_TRUE(smOutgoingMessages.ParseFromArray(receivedMsg2.data(), receivedMsg2.size()));
    EXPECT_TRUE(smOutgoingMessages.has_node_config_status());
}

TEST_F(CommunicationOpenManagerTest, TestSyncClockRequest)
{
    EXPECT_EQ(mCommManager->Init(mConfig, mServer.value()), ErrorEnum::eNone);

    auto err = mIAMConnection.Init(mConfig.mIAMConfig.mOpenPort, IAMHandler, *mCommManager);
    EXPECT_EQ(err, ErrorEnum::eNone);

    err = mCMConnection.Init(mConfig, CMHandler, *mCommManager);
    EXPECT_EQ(err, ErrorEnum::eNone);

    EXPECT_EQ(mCommManager->Start(), ErrorEnum::eNone);
    EXPECT_EQ(mIAMConnection.Start(), ErrorEnum::eNone);
    EXPECT_EQ(mCMConnection.Start(), ErrorEnum::eNone);

    servicemanager::v4::SMOutgoingMessages outgoingMessages;
    outgoingMessages.mutable_clock_sync_request();

    std::vector<uint8_t> messageData(outgoingMessages.ByteSizeLong());
    EXPECT_TRUE(outgoingMessages.SerializeToArray(messageData.data(), messageData.size()));
    auto protobufHeader = PrepareProtobufHeader(messageData.size());
    protobufHeader.insert(protobufHeader.end(), messageData.begin(), messageData.end());
    EXPECT_EQ(mCMClientChannel->Write(protobufHeader), ErrorEnum::eNone);

    std::vector<uint8_t> message(sizeof(AosProtobufHeader));
    EXPECT_EQ(mCMClientChannel->Read(message), ErrorEnum::eNone);
    auto header = ParseProtobufHeader(message);
    message.clear();
    message.resize(header.mDataSize);

    EXPECT_EQ(mCMClientChannel->Read(message), ErrorEnum::eNone);
    servicemanager::v4::SMIncomingMessages incomingMessages;
    EXPECT_TRUE(incomingMessages.ParseFromArray(message.data(), message.size()));
    EXPECT_TRUE(incomingMessages.has_clock_sync());

    auto currentTime = incomingMessages.clock_sync().current_time();
    auto now         = std::chrono::system_clock::now();
    auto diff        = std::chrono::duration_cast<std::chrono::seconds>(
        now - std::chrono::system_clock::from_time_t(currentTime.seconds()));
    EXPECT_LT(diff.count(), 1);
}

TEST_F(CommunicationOpenManagerTest, TestSendIAMIncomingMessages)
{
    EXPECT_EQ(mCommManager->Init(mConfig, mServer.value()), ErrorEnum::eNone);

    auto err = mIAMConnection.Init(mConfig.mIAMConfig.mOpenPort, IAMHandler, *mCommManager);
    EXPECT_EQ(err, ErrorEnum::eNone);

    err = mCMConnection.Init(mConfig, CMHandler, *mCommManager);
    EXPECT_EQ(err, ErrorEnum::eNone);

    EXPECT_EQ(mCommManager->Start(), ErrorEnum::eNone);
    EXPECT_EQ(mIAMConnection.Start(), ErrorEnum::eNone);
    EXPECT_EQ(mCMConnection.Start(), ErrorEnum::eNone);

    iamanager::v5::IAMIncomingMessages outgoingMsg;
    outgoingMsg.mutable_start_provisioning_request();
    std::vector<uint8_t> messageData(outgoingMsg.ByteSizeLong());
    EXPECT_TRUE(outgoingMsg.SerializeToArray(messageData.data(), messageData.size()));

    EXPECT_EQ(IAMHandler.SetIncomingMessages(messageData), ErrorEnum::eNone);

    std::vector<uint8_t> message(sizeof(AosProtobufHeader));
    EXPECT_EQ(mIAMClientChannel->Read(message), ErrorEnum::eNone);
    auto header = ParseProtobufHeader(message);
    message.clear();
    message.resize(header.mDataSize);

    EXPECT_EQ(mIAMClientChannel->Read(message), ErrorEnum::eNone);
    iamanager::v5::IAMIncomingMessages incomingMessages;
    EXPECT_TRUE(incomingMessages.ParseFromArray(message.data(), message.size()));
    EXPECT_TRUE(incomingMessages.has_start_provisioning_request());
}

TEST_F(CommunicationOpenManagerTest, TestIAMFlow)
{
    EXPECT_EQ(mCommManager->Init(mConfig, mServer.value()), ErrorEnum::eNone);

    auto err = mIAMConnection.Init(mConfig.mIAMConfig.mOpenPort, IAMHandler, *mCommManager);
    EXPECT_EQ(err, ErrorEnum::eNone);

    err = mCMConnection.Init(mConfig, CMHandler, *mCommManager);
    EXPECT_EQ(err, ErrorEnum::eNone);

    EXPECT_EQ(mCommManager->Start(), ErrorEnum::eNone);
    EXPECT_EQ(mIAMConnection.Start(), ErrorEnum::eNone);
    EXPECT_EQ(mCMConnection.Start(), ErrorEnum::eNone);

    iamanager::v5::IAMIncomingMessages outgoingMsg;
    outgoingMsg.mutable_start_provisioning_request();
    std::vector<uint8_t> messageData(outgoingMsg.ByteSizeLong());
    EXPECT_TRUE(outgoingMsg.SerializeToArray(messageData.data(), messageData.size()));

    EXPECT_EQ(IAMHandler.SetIncomingMessages(messageData), ErrorEnum::eNone);

    std::vector<uint8_t> message(sizeof(AosProtobufHeader));
    EXPECT_EQ(mIAMClientChannel->Read(message), ErrorEnum::eNone);
    auto header = ParseProtobufHeader(message);
    message.clear();
    message.resize(header.mDataSize);

    EXPECT_EQ(mIAMClientChannel->Read(message), ErrorEnum::eNone);
    iamanager::v5::IAMIncomingMessages incomingMessages;
    EXPECT_TRUE(incomingMessages.ParseFromArray(message.data(), message.size()));
    EXPECT_TRUE(incomingMessages.has_start_provisioning_request());

    iamanager::v5::IAMOutgoingMessages outgoingMsg2;
    outgoingMsg2.mutable_start_provisioning_response();
    std::vector<uint8_t> messageData2(outgoingMsg2.ByteSizeLong());
    EXPECT_TRUE(outgoingMsg2.SerializeToArray(messageData2.data(), messageData2.size()));

    auto protobufHeader = PrepareProtobufHeader(messageData2.size());
    protobufHeader.insert(protobufHeader.end(), messageData2.begin(), messageData2.end());
    EXPECT_EQ(mIAMClientChannel->Write(protobufHeader), ErrorEnum::eNone);

    auto [receivedMsg, errReceive] = IAMHandler.GetOutgoingMessages();
    EXPECT_EQ(errReceive, ErrorEnum::eNone);
    EXPECT_TRUE(outgoingMsg2.ParseFromArray(receivedMsg.data(), receivedMsg.size()));
    EXPECT_TRUE(outgoingMsg2.has_start_provisioning_response());
}

} // namespace aos::mp::communication
