/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <optional>

#include <gtest/gtest.h>

#include <core/common/tests/mocks/certprovidermock.hpp>
#include <core/common/tests/utils/log.hpp>

#include <common/iamclient/tests/mocks/tlscredentialsmock.hpp>

#include <mp/iamclient/iamclient.hpp>

#include "stubs/iamserver.hpp"

using namespace testing;

namespace aos::mp::iamclient {

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class IamClientTest : public Test {
public:
    IamClientTest()
    {
        mConfig.mIAMConfig.mIAMPublicServerURL        = "localhost:8002";
        mConfig.mIAMConfig.mIAMMainPublicServerURL    = "localhost:8002";
        mConfig.mIAMConfig.mIAMMainProtectedServerURL = "localhost:8002";
    }

protected:
    void SetUp() override
    {
        tests::utils::InitLog();

        mIAMServerStub.emplace();
        mClient.emplace();
    }

    void TearDown() override { mClient->Stop(); }

    std::optional<TestIAMServer> mIAMServerStub;

    std::optional<IAMClient>   mClient;
    crypto::x509::ProviderItf* mCryptoProvider {};
    config::Config             mConfig {};
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(IamClientTest, RegisterNodeOutgoingMessages)
{
    aos::iamclient::CertProviderMock certProvider {};
    TLSCredentialsMock               tlsCredentials {};

    auto err = mClient->Init(mConfig.mIAMConfig, certProvider, tlsCredentials, true);
    ASSERT_EQ(err, ErrorEnum::eNone);

    err = mClient->Start();
    ASSERT_EQ(err, ErrorEnum::eNone);

    EXPECT_TRUE(mIAMServerStub->WaitForConnection());

    iamanager::v6::IAMOutgoingMessages outgoingMsg;

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
}

TEST_F(IamClientTest, RegisterNodeIncomingMessages)
{
    aos::iamclient::CertProviderMock certProvider {};
    TLSCredentialsMock               tlsCredentials {};

    auto err = mClient->Init(mConfig.mIAMConfig, certProvider, tlsCredentials, true);
    ASSERT_EQ(err, ErrorEnum::eNone);

    err = mClient->Start();
    ASSERT_EQ(err, ErrorEnum::eNone);

    EXPECT_TRUE(mIAMServerStub->WaitForConnection());

    iamanager::v6::IAMIncomingMessages incomingMsg;

    // receive StartProvisioningRequest
    incomingMsg.mutable_start_provisioning_request();
    EXPECT_TRUE(mIAMServerStub->SendIncomingMessage(incomingMsg));
    auto res = mClient->ReceiveMessages();
    EXPECT_EQ(res.mError, ErrorEnum::eNone);
    EXPECT_TRUE(incomingMsg.ParseFromArray(res.mValue.data(), res.mValue.size()));
    EXPECT_TRUE(incomingMsg.has_start_provisioning_request());

    // receive GetCertTypesRequest
    incomingMsg.mutable_get_cert_types_request();
    EXPECT_TRUE(mIAMServerStub->SendIncomingMessage(incomingMsg));
    res = mClient->ReceiveMessages();
    EXPECT_EQ(res.mError, ErrorEnum::eNone);
    EXPECT_TRUE(incomingMsg.ParseFromArray(res.mValue.data(), res.mValue.size()));
    EXPECT_TRUE(incomingMsg.has_get_cert_types_request());

    // receive FinishProvisioningRequest
    incomingMsg.mutable_finish_provisioning_request();
    EXPECT_TRUE(mIAMServerStub->SendIncomingMessage(incomingMsg));
    res = mClient->ReceiveMessages();
    EXPECT_EQ(res.mError, ErrorEnum::eNone);
    EXPECT_TRUE(incomingMsg.ParseFromArray(res.mValue.data(), res.mValue.size()));
    EXPECT_TRUE(incomingMsg.has_finish_provisioning_request());

    // receive DeprovisionRequest
    incomingMsg.mutable_deprovision_request();
    EXPECT_TRUE(mIAMServerStub->SendIncomingMessage(incomingMsg));
    res = mClient->ReceiveMessages();
    EXPECT_EQ(res.mError, ErrorEnum::eNone);
    EXPECT_TRUE(incomingMsg.ParseFromArray(res.mValue.data(), res.mValue.size()));
    EXPECT_TRUE(incomingMsg.has_deprovision_request());

    // receive PauseNodeRequest
    incomingMsg.mutable_pause_node_request();
    EXPECT_TRUE(mIAMServerStub->SendIncomingMessage(incomingMsg));
    res = mClient->ReceiveMessages();
    EXPECT_EQ(res.mError, ErrorEnum::eNone);
    EXPECT_TRUE(incomingMsg.ParseFromArray(res.mValue.data(), res.mValue.size()));
    EXPECT_TRUE(incomingMsg.has_pause_node_request());

    // receive ResumeNodeRequest
    incomingMsg.mutable_resume_node_request();
    EXPECT_TRUE(mIAMServerStub->SendIncomingMessage(incomingMsg));
    res = mClient->ReceiveMessages();
    EXPECT_EQ(res.mError, ErrorEnum::eNone);
    EXPECT_TRUE(incomingMsg.ParseFromArray(res.mValue.data(), res.mValue.size()));
    EXPECT_TRUE(incomingMsg.has_resume_node_request());

    // receive CreateKeyRequest
    incomingMsg.mutable_create_key_request();
    EXPECT_TRUE(mIAMServerStub->SendIncomingMessage(incomingMsg));
    res = mClient->ReceiveMessages();
    EXPECT_EQ(res.mError, ErrorEnum::eNone);
    EXPECT_TRUE(incomingMsg.ParseFromArray(res.mValue.data(), res.mValue.size()));
    EXPECT_TRUE(incomingMsg.has_create_key_request());

    // receive ApplyCertRequest
    incomingMsg.mutable_apply_cert_request();
    EXPECT_TRUE(mIAMServerStub->SendIncomingMessage(incomingMsg));
    res = mClient->ReceiveMessages();
    EXPECT_EQ(res.mError, ErrorEnum::eNone);
    EXPECT_TRUE(incomingMsg.ParseFromArray(res.mValue.data(), res.mValue.size()));
    EXPECT_TRUE(incomingMsg.has_apply_cert_request());
}

TEST_F(IamClientTest, CertChanged)
{
    aos::iamclient::CertProviderMock certProvider {};
    TLSCredentialsMock               tlsCredentials {};
    EXPECT_CALL(tlsCredentials, GetMTLSClientCredentials(_))
        .Times(2)
        .WillRepeatedly(testing::Return(std::shared_ptr<grpc::ChannelCredentials>(grpc::InsecureChannelCredentials())));
    EXPECT_CALL(certProvider, SubscribeListener(_, _)).WillOnce(testing::Return(ErrorEnum::eNone));
    EXPECT_CALL(certProvider, UnsubscribeListener(_)).WillOnce(testing::Return(ErrorEnum::eNone));

    mConfig.mIAMConfig.mCertStorage = "iam";

    auto err = mClient->Init(mConfig.mIAMConfig, certProvider, tlsCredentials);
    ASSERT_EQ(err, ErrorEnum::eNone);

    err = mClient->Start();
    ASSERT_EQ(err, ErrorEnum::eNone);

    EXPECT_TRUE(mIAMServerStub->WaitForConnection());

    err = mClient->Reconnect();
    EXPECT_EQ(err, ErrorEnum::eNone);

    EXPECT_TRUE(mIAMServerStub->WaitForDisconnection());
    EXPECT_TRUE(mIAMServerStub->WaitForConnection());

    mClient->Stop();
}

} // namespace aos::mp::iamclient
