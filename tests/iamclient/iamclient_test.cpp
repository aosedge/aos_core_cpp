/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <optional>

#include <gtest/gtest.h>

#include <test/utils/log.hpp>

#include "iamclient/iamclient.hpp"
#include "iamclient/types.hpp"
#include "stubs/iamserver.hpp"

using namespace testing;
using namespace aos::mp::iamclient;

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class IamClientTest : public Test {
public:
    IamClientTest() { mConfig.mIAMConfig.mIAMPublicServerURL = "localhost:8002"; }

protected:
    void SetUp() override
    {
        aos::InitLog();

        mIAMServerStub.emplace();
        mClient.emplace();

        auto getMTLSCredentials = [this](const aos::iam::certhandler::CertInfo& certInfo, const aos::String&,
                                      aos::cryptoutils::CertLoaderItf&, aos::crypto::x509::ProviderItf&) {
            mCertInfo = certInfo;

            return nullptr;
        };

        auto err = mClient->Init(mConfig, *mCertLoader, *mCryptoProvider, true, std::move(getMTLSCredentials));

        ASSERT_EQ(err, aos::ErrorEnum::eNone);
    }

    std::optional<TestIAMServer> mIAMServerStub;

    std::optional<IAMClient>         mClient;
    aos::cryptoutils::CertLoaderItf* mCertLoader {};
    aos::crypto::x509::ProviderItf*  mCryptoProvider {};
    aos::mp::config::Config          mConfig {};
    aos::iam::certhandler::CertInfo  mCertInfo {};
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(IamClientTest, GetClientMTLSConfig)
{
    aos::iam::certhandler::CertInfo certInfo;
    certInfo.mCertURL = "client_cert";
    certInfo.mKeyURL  = "client_key";

    mIAMServerStub->SetCertInfo(certInfo);

    auto [_, err] = mClient->GetMTLSConfig("client_cert_type");

    EXPECT_EQ(err, aos::ErrorEnum::eNone);
    EXPECT_EQ(mIAMServerStub->GetCertType(), "client_cert_type");
    EXPECT_EQ(mCertInfo, certInfo);
}

TEST_F(IamClientTest, RegisterNodeOutgoingMessages)
{
    auto& handler = mClient->GetPublicHandler();
    handler.OnConnected();
    EXPECT_TRUE(mIAMServerStub->WaitForConnection());

    iamanager::v5::IAMOutgoingMessages outgoingMsg;

    // send StartProvisioningResponse
    outgoingMsg.mutable_start_provisioning_response();
    std::vector<uint8_t> data(outgoingMsg.ByteSizeLong());
    EXPECT_TRUE(outgoingMsg.SerializeToArray(data.data(), data.size()));
    handler.SendMessages(std::move(data));
    mIAMServerStub->WaitResponse();
    outgoingMsg = mIAMServerStub->GetOutgoingMessage();
    EXPECT_TRUE(outgoingMsg.has_start_provisioning_response());

    // send FinishProvisioningResponse
    outgoingMsg.mutable_finish_provisioning_response();
    data.resize(outgoingMsg.ByteSizeLong());
    EXPECT_TRUE(outgoingMsg.SerializeToArray(data.data(), data.size()));
    handler.SendMessages(std::move(data));
    mIAMServerStub->WaitResponse();
    outgoingMsg = mIAMServerStub->GetOutgoingMessage();
    EXPECT_TRUE(outgoingMsg.has_finish_provisioning_response());

    // send DeprovisionResponse
    outgoingMsg.mutable_deprovision_response();
    data.resize(outgoingMsg.ByteSizeLong());
    EXPECT_TRUE(outgoingMsg.SerializeToArray(data.data(), data.size()));
    handler.SendMessages(std::move(data));
    mIAMServerStub->WaitResponse();
    outgoingMsg = mIAMServerStub->GetOutgoingMessage();
    EXPECT_TRUE(outgoingMsg.has_deprovision_response());

    // send PauseNodeResponse
    outgoingMsg.mutable_pause_node_response();
    data.resize(outgoingMsg.ByteSizeLong());
    EXPECT_TRUE(outgoingMsg.SerializeToArray(data.data(), data.size()));
    handler.SendMessages(std::move(data));
    mIAMServerStub->WaitResponse();
    outgoingMsg = mIAMServerStub->GetOutgoingMessage();
    EXPECT_TRUE(outgoingMsg.has_pause_node_response());

    // send ResumeNodeResponse
    outgoingMsg.mutable_resume_node_response();
    data.resize(outgoingMsg.ByteSizeLong());
    EXPECT_TRUE(outgoingMsg.SerializeToArray(data.data(), data.size()));
    handler.SendMessages(std::move(data));
    mIAMServerStub->WaitResponse();
    outgoingMsg = mIAMServerStub->GetOutgoingMessage();
    EXPECT_TRUE(outgoingMsg.has_resume_node_response());

    // send CreateKeyResponse
    outgoingMsg.mutable_create_key_response();
    data.resize(outgoingMsg.ByteSizeLong());
    EXPECT_TRUE(outgoingMsg.SerializeToArray(data.data(), data.size()));
    handler.SendMessages(std::move(data));
    mIAMServerStub->WaitResponse();
    outgoingMsg = mIAMServerStub->GetOutgoingMessage();
    EXPECT_TRUE(outgoingMsg.has_create_key_response());

    // send ApplyCertResponse
    outgoingMsg.mutable_apply_cert_response();
    data.resize(outgoingMsg.ByteSizeLong());
    EXPECT_TRUE(outgoingMsg.SerializeToArray(data.data(), data.size()));
    handler.SendMessages(std::move(data));
    mIAMServerStub->WaitResponse();
    outgoingMsg = mIAMServerStub->GetOutgoingMessage();
    EXPECT_TRUE(outgoingMsg.has_apply_cert_response());

    // send CertTypesResponse
    outgoingMsg.mutable_cert_types_response();
    data.resize(outgoingMsg.ByteSizeLong());
    EXPECT_TRUE(outgoingMsg.SerializeToArray(data.data(), data.size()));
    handler.SendMessages(std::move(data));
    mIAMServerStub->WaitResponse();
    outgoingMsg = mIAMServerStub->GetOutgoingMessage();
    EXPECT_TRUE(outgoingMsg.has_cert_types_response());

    handler.OnDisconnected();
}

TEST_F(IamClientTest, RegisterNodeIncomingMessages)
{
    auto& handler = mClient->GetPublicHandler();
    handler.OnConnected();

    EXPECT_TRUE(mIAMServerStub->WaitForConnection());

    iamanager::v5::IAMIncomingMessages incomingMsg;

    // receive StartProvisioningRequest
    incomingMsg.mutable_start_provisioning_request();
    EXPECT_TRUE(mIAMServerStub->SendIncomingMessage(incomingMsg));
    auto res = handler.ReceiveMessages();
    EXPECT_EQ(res.mError, aos::ErrorEnum::eNone);
    EXPECT_TRUE(incomingMsg.ParseFromArray(res.mValue.data(), res.mValue.size()));
    EXPECT_TRUE(incomingMsg.has_start_provisioning_request());

    // receive GetCertTypesRequest
    incomingMsg.mutable_get_cert_types_request();
    EXPECT_TRUE(mIAMServerStub->SendIncomingMessage(incomingMsg));
    res = handler.ReceiveMessages();
    EXPECT_EQ(res.mError, aos::ErrorEnum::eNone);
    EXPECT_TRUE(incomingMsg.ParseFromArray(res.mValue.data(), res.mValue.size()));
    EXPECT_TRUE(incomingMsg.has_get_cert_types_request());

    // receive FinishProvisioningRequest
    incomingMsg.mutable_finish_provisioning_request();
    EXPECT_TRUE(mIAMServerStub->SendIncomingMessage(incomingMsg));
    res = handler.ReceiveMessages();
    EXPECT_EQ(res.mError, aos::ErrorEnum::eNone);
    EXPECT_TRUE(incomingMsg.ParseFromArray(res.mValue.data(), res.mValue.size()));
    EXPECT_TRUE(incomingMsg.has_finish_provisioning_request());

    // receive DeprovisionRequest
    incomingMsg.mutable_deprovision_request();
    EXPECT_TRUE(mIAMServerStub->SendIncomingMessage(incomingMsg));
    res = handler.ReceiveMessages();
    EXPECT_EQ(res.mError, aos::ErrorEnum::eNone);
    EXPECT_TRUE(incomingMsg.ParseFromArray(res.mValue.data(), res.mValue.size()));
    EXPECT_TRUE(incomingMsg.has_deprovision_request());

    // receive PauseNodeRequest
    incomingMsg.mutable_pause_node_request();
    EXPECT_TRUE(mIAMServerStub->SendIncomingMessage(incomingMsg));
    res = handler.ReceiveMessages();
    EXPECT_EQ(res.mError, aos::ErrorEnum::eNone);
    EXPECT_TRUE(incomingMsg.ParseFromArray(res.mValue.data(), res.mValue.size()));
    EXPECT_TRUE(incomingMsg.has_pause_node_request());

    // receive ResumeNodeRequest
    incomingMsg.mutable_resume_node_request();
    EXPECT_TRUE(mIAMServerStub->SendIncomingMessage(incomingMsg));
    res = handler.ReceiveMessages();
    EXPECT_EQ(res.mError, aos::ErrorEnum::eNone);
    EXPECT_TRUE(incomingMsg.ParseFromArray(res.mValue.data(), res.mValue.size()));
    EXPECT_TRUE(incomingMsg.has_resume_node_request());

    // receive CreateKeyRequest
    incomingMsg.mutable_create_key_request();
    EXPECT_TRUE(mIAMServerStub->SendIncomingMessage(incomingMsg));
    res = handler.ReceiveMessages();
    EXPECT_EQ(res.mError, aos::ErrorEnum::eNone);
    EXPECT_TRUE(incomingMsg.ParseFromArray(res.mValue.data(), res.mValue.size()));
    EXPECT_TRUE(incomingMsg.has_create_key_request());

    // receive ApplyCertRequest
    incomingMsg.mutable_apply_cert_request();
    EXPECT_TRUE(mIAMServerStub->SendIncomingMessage(incomingMsg));
    res = handler.ReceiveMessages();
    EXPECT_EQ(res.mError, aos::ErrorEnum::eNone);
    EXPECT_TRUE(incomingMsg.ParseFromArray(res.mValue.data(), res.mValue.size()));
    EXPECT_TRUE(incomingMsg.has_apply_cert_request());

    handler.OnDisconnected();
}
