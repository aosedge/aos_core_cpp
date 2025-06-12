/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <optional>

#include <gtest/gtest.h>

#include <aos/test/log.hpp>

#include "mp/iamclient/publicnodeclient.hpp"

#include "mocks/certprovider.hpp"
#include "stubs/iamserver.hpp"

using namespace testing;
using namespace aos::mp::iamclient;

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class IamClientTest : public Test {
public:
    IamClientTest()
    {
        mConfig.mIAMConfig.mIAMPublicServerURL    = "localhost:8002";
        mConfig.mIAMConfig.mIAMProtectedServerURL = "localhost:8002";
    }

protected:
    void SetUp() override
    {
        aos::test::InitLog();

        mIAMServerStub.emplace();
        mClient.emplace();
    }

    std::optional<TestIAMServer> mIAMServerStub;

    std::optional<PublicNodeClient> mClient;
    aos::crypto::x509::ProviderItf* mCryptoProvider {};
    aos::mp::config::Config         mConfig {};
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(IamClientTest, RegisterNodeOutgoingMessages)
{
    MockCertProvider certProvider {};
    EXPECT_CALL(certProvider, GetTLSClientCredentials()).WillOnce(testing::Return(grpc::InsecureChannelCredentials()));

    auto err = mClient->Init(mConfig.mIAMConfig, certProvider);
    ASSERT_EQ(err, aos::ErrorEnum::eNone);

    mClient->OnConnected();
    EXPECT_TRUE(mIAMServerStub->WaitForConnection());

    iamanager::v5::IAMOutgoingMessages outgoingMsg;

    // send StartProvisioningResponse
    outgoingMsg.mutable_start_provisioning_response();
    std::vector<uint8_t> data(outgoingMsg.ByteSizeLong());
    EXPECT_TRUE(outgoingMsg.SerializeToArray(data.data(), data.size()));
    mClient->SendMessages(std::move(data));
    mIAMServerStub->WaitResponse();
    outgoingMsg = mIAMServerStub->GetOutgoingMessage();
    EXPECT_TRUE(outgoingMsg.has_start_provisioning_response());

    // send FinishProvisioningResponse
    outgoingMsg.mutable_finish_provisioning_response();
    data.resize(outgoingMsg.ByteSizeLong());
    EXPECT_TRUE(outgoingMsg.SerializeToArray(data.data(), data.size()));
    mClient->SendMessages(std::move(data));
    mIAMServerStub->WaitResponse();
    outgoingMsg = mIAMServerStub->GetOutgoingMessage();
    EXPECT_TRUE(outgoingMsg.has_finish_provisioning_response());

    // send DeprovisionResponse
    outgoingMsg.mutable_deprovision_response();
    data.resize(outgoingMsg.ByteSizeLong());
    EXPECT_TRUE(outgoingMsg.SerializeToArray(data.data(), data.size()));
    mClient->SendMessages(std::move(data));
    mIAMServerStub->WaitResponse();
    outgoingMsg = mIAMServerStub->GetOutgoingMessage();
    EXPECT_TRUE(outgoingMsg.has_deprovision_response());

    // send PauseNodeResponse
    outgoingMsg.mutable_pause_node_response();
    data.resize(outgoingMsg.ByteSizeLong());
    EXPECT_TRUE(outgoingMsg.SerializeToArray(data.data(), data.size()));
    mClient->SendMessages(std::move(data));
    mIAMServerStub->WaitResponse();
    outgoingMsg = mIAMServerStub->GetOutgoingMessage();
    EXPECT_TRUE(outgoingMsg.has_pause_node_response());

    // send ResumeNodeResponse
    outgoingMsg.mutable_resume_node_response();
    data.resize(outgoingMsg.ByteSizeLong());
    EXPECT_TRUE(outgoingMsg.SerializeToArray(data.data(), data.size()));
    mClient->SendMessages(std::move(data));
    mIAMServerStub->WaitResponse();
    outgoingMsg = mIAMServerStub->GetOutgoingMessage();
    EXPECT_TRUE(outgoingMsg.has_resume_node_response());

    // send CreateKeyResponse
    outgoingMsg.mutable_create_key_response();
    data.resize(outgoingMsg.ByteSizeLong());
    EXPECT_TRUE(outgoingMsg.SerializeToArray(data.data(), data.size()));
    mClient->SendMessages(std::move(data));
    mIAMServerStub->WaitResponse();
    outgoingMsg = mIAMServerStub->GetOutgoingMessage();
    EXPECT_TRUE(outgoingMsg.has_create_key_response());

    // send ApplyCertResponse
    outgoingMsg.mutable_apply_cert_response();
    data.resize(outgoingMsg.ByteSizeLong());
    EXPECT_TRUE(outgoingMsg.SerializeToArray(data.data(), data.size()));
    mClient->SendMessages(std::move(data));
    mIAMServerStub->WaitResponse();
    outgoingMsg = mIAMServerStub->GetOutgoingMessage();
    EXPECT_TRUE(outgoingMsg.has_apply_cert_response());

    // send CertTypesResponse
    outgoingMsg.mutable_cert_types_response();
    data.resize(outgoingMsg.ByteSizeLong());
    EXPECT_TRUE(outgoingMsg.SerializeToArray(data.data(), data.size()));
    mClient->SendMessages(std::move(data));
    mIAMServerStub->WaitResponse();
    outgoingMsg = mIAMServerStub->GetOutgoingMessage();
    EXPECT_TRUE(outgoingMsg.has_cert_types_response());

    mClient->OnDisconnected();
}

TEST_F(IamClientTest, RegisterNodeIncomingMessages)
{
    MockCertProvider certProvider {};
    EXPECT_CALL(certProvider, GetTLSClientCredentials()).WillOnce(testing::Return(grpc::InsecureChannelCredentials()));

    auto err = mClient->Init(mConfig.mIAMConfig, certProvider);
    ASSERT_EQ(err, aos::ErrorEnum::eNone);

    mClient->OnConnected();
    EXPECT_TRUE(mIAMServerStub->WaitForConnection());

    iamanager::v5::IAMIncomingMessages incomingMsg;

    // receive StartProvisioningRequest
    incomingMsg.mutable_start_provisioning_request();
    EXPECT_TRUE(mIAMServerStub->SendIncomingMessage(incomingMsg));
    auto res = mClient->ReceiveMessages();
    EXPECT_EQ(res.mError, aos::ErrorEnum::eNone);
    EXPECT_TRUE(incomingMsg.ParseFromArray(res.mValue.data(), res.mValue.size()));
    EXPECT_TRUE(incomingMsg.has_start_provisioning_request());

    // receive GetCertTypesRequest
    incomingMsg.mutable_get_cert_types_request();
    EXPECT_TRUE(mIAMServerStub->SendIncomingMessage(incomingMsg));
    res = mClient->ReceiveMessages();
    EXPECT_EQ(res.mError, aos::ErrorEnum::eNone);
    EXPECT_TRUE(incomingMsg.ParseFromArray(res.mValue.data(), res.mValue.size()));
    EXPECT_TRUE(incomingMsg.has_get_cert_types_request());

    // receive FinishProvisioningRequest
    incomingMsg.mutable_finish_provisioning_request();
    EXPECT_TRUE(mIAMServerStub->SendIncomingMessage(incomingMsg));
    res = mClient->ReceiveMessages();
    EXPECT_EQ(res.mError, aos::ErrorEnum::eNone);
    EXPECT_TRUE(incomingMsg.ParseFromArray(res.mValue.data(), res.mValue.size()));
    EXPECT_TRUE(incomingMsg.has_finish_provisioning_request());

    // receive DeprovisionRequest
    incomingMsg.mutable_deprovision_request();
    EXPECT_TRUE(mIAMServerStub->SendIncomingMessage(incomingMsg));
    res = mClient->ReceiveMessages();
    EXPECT_EQ(res.mError, aos::ErrorEnum::eNone);
    EXPECT_TRUE(incomingMsg.ParseFromArray(res.mValue.data(), res.mValue.size()));
    EXPECT_TRUE(incomingMsg.has_deprovision_request());

    // receive PauseNodeRequest
    incomingMsg.mutable_pause_node_request();
    EXPECT_TRUE(mIAMServerStub->SendIncomingMessage(incomingMsg));
    res = mClient->ReceiveMessages();
    EXPECT_EQ(res.mError, aos::ErrorEnum::eNone);
    EXPECT_TRUE(incomingMsg.ParseFromArray(res.mValue.data(), res.mValue.size()));
    EXPECT_TRUE(incomingMsg.has_pause_node_request());

    // receive ResumeNodeRequest
    incomingMsg.mutable_resume_node_request();
    EXPECT_TRUE(mIAMServerStub->SendIncomingMessage(incomingMsg));
    res = mClient->ReceiveMessages();
    EXPECT_EQ(res.mError, aos::ErrorEnum::eNone);
    EXPECT_TRUE(incomingMsg.ParseFromArray(res.mValue.data(), res.mValue.size()));
    EXPECT_TRUE(incomingMsg.has_resume_node_request());

    // receive CreateKeyRequest
    incomingMsg.mutable_create_key_request();
    EXPECT_TRUE(mIAMServerStub->SendIncomingMessage(incomingMsg));
    res = mClient->ReceiveMessages();
    EXPECT_EQ(res.mError, aos::ErrorEnum::eNone);
    EXPECT_TRUE(incomingMsg.ParseFromArray(res.mValue.data(), res.mValue.size()));
    EXPECT_TRUE(incomingMsg.has_create_key_request());

    // receive ApplyCertRequest
    incomingMsg.mutable_apply_cert_request();
    EXPECT_TRUE(mIAMServerStub->SendIncomingMessage(incomingMsg));
    res = mClient->ReceiveMessages();
    EXPECT_EQ(res.mError, aos::ErrorEnum::eNone);
    EXPECT_TRUE(incomingMsg.ParseFromArray(res.mValue.data(), res.mValue.size()));
    EXPECT_TRUE(incomingMsg.has_apply_cert_request());

    mClient->OnDisconnected();
}

TEST_F(IamClientTest, CertChanged)
{
    MockCertProvider certProvider {};
    EXPECT_CALL(certProvider, GetMTLSClientCredentials(_))
        .Times(2)
        .WillRepeatedly(testing::Return(std::shared_ptr<grpc::ChannelCredentials>(grpc::InsecureChannelCredentials())));
    auto err = mClient->Init(mConfig.mIAMConfig, certProvider, false);
    ASSERT_EQ(err, aos::ErrorEnum::eNone);

    mClient->OnConnected();
    EXPECT_TRUE(mIAMServerStub->WaitForConnection());

    mClient->OnCertChanged(aos::iam::certhandler::CertInfo {});

    EXPECT_TRUE(mIAMServerStub->WaitForDisconnection());
    EXPECT_TRUE(mIAMServerStub->WaitForConnection());

    mClient->OnDisconnected();
}
