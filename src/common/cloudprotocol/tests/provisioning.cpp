/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <array>

#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>

#include <common/cloudprotocol/provisioning.hpp>
#include <common/utils/json.hpp>

using namespace testing;

namespace aos::common::cloudprotocol {

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class CloudProtocolProvisioning : public Test {
public:
    void SetUp() override { tests::utils::InitLog(); }
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(CloudProtocolProvisioning, EmptyStartProvisioningRequest)
{
    aos::cloudprotocol::StartProvisioningRequest request;

    auto json = Poco::makeShared<Poco::JSON::Object>();
    ASSERT_EQ(ToJSON(request, *json), ErrorEnum::eNone);

    utils::CaseInsensitiveObjectWrapper jsonWrapper(json);

    EXPECT_EQ(jsonWrapper.GetValue<std::string>("messageType", ""), "startProvisioningRequest");
    EXPECT_EQ(jsonWrapper.GetValue<std::string>("nodeId", "empty expected"), "");
    EXPECT_EQ(jsonWrapper.GetValue<std::string>("password", "empty expected"), "");

    aos::cloudprotocol::StartProvisioningRequest parsedRequest;
    ASSERT_EQ(FromJSON(jsonWrapper, parsedRequest), ErrorEnum::eNone);

    ASSERT_EQ(request, parsedRequest);
}

TEST_F(CloudProtocolProvisioning, StartProvisioningRequest)
{
    aos::cloudprotocol::StartProvisioningRequest request;
    request.mNodeID.Assign("testNode");
    request.mPassword.Assign("testPassword");

    auto json = Poco::makeShared<Poco::JSON::Object>();
    ASSERT_EQ(ToJSON(request, *json), ErrorEnum::eNone);

    utils::CaseInsensitiveObjectWrapper jsonWrapper(json);

    EXPECT_EQ(jsonWrapper.GetValue<std::string>("messageType", ""), "startProvisioningRequest");
    EXPECT_EQ(jsonWrapper.GetValue<std::string>("nodeId", ""), "testNode");
    EXPECT_EQ(jsonWrapper.GetValue<std::string>("password", ""), "testPassword");

    aos::cloudprotocol::StartProvisioningRequest parsedRequest;
    ASSERT_EQ(FromJSON(jsonWrapper, parsedRequest), ErrorEnum::eNone);

    ASSERT_EQ(request, parsedRequest);
}

TEST_F(CloudProtocolProvisioning, EmptyStartProvisioningResponse)
{
    aos::cloudprotocol::StartProvisioningResponse response;

    auto json = Poco::makeShared<Poco::JSON::Object>();
    ASSERT_EQ(ToJSON(response, *json), ErrorEnum::eNone);

    utils::CaseInsensitiveObjectWrapper jsonWrapper(json);

    EXPECT_EQ(jsonWrapper.GetValue<std::string>("messageType", ""), "startProvisioningResponse");
    EXPECT_EQ(jsonWrapper.GetValue<std::string>("nodeId", "empty expected"), "");
    EXPECT_TRUE(jsonWrapper.Has("csrs"));
    EXPECT_FALSE(jsonWrapper.Has("errorInfo"));

    aos::cloudprotocol::StartProvisioningResponse parsedResponse;
    ASSERT_EQ(FromJSON(jsonWrapper, parsedResponse), ErrorEnum::eNone);

    ASSERT_EQ(response, parsedResponse);
}

TEST_F(CloudProtocolProvisioning, StartProvisioningResponse)
{
    auto response = std::make_unique<aos::cloudprotocol::StartProvisioningResponse>();
    response->mNodeID.Assign("testNode");
    response->mError = Error(ErrorEnum::eFailed, "Test error");

    response->mCSRs.EmplaceBack();
    response->mCSRs.Back().mCsr.Assign("csr-1");
    response->mCSRs.Back().mNodeID.Assign("node-1");
    response->mCSRs.Back().mType = CertTypeEnum::eOffline;

    response->mCSRs.EmplaceBack();
    response->mCSRs.Back().mCsr.Assign("csr-2");
    response->mCSRs.Back().mNodeID.Assign("node-2");
    response->mCSRs.Back().mType = CertTypeEnum::eOnline;

    response->mCSRs.EmplaceBack();

    auto json = Poco::makeShared<Poco::JSON::Object>();
    ASSERT_EQ(ToJSON(*response, *json), ErrorEnum::eNone);

    utils::CaseInsensitiveObjectWrapper jsonWrapper(json);

    EXPECT_EQ(jsonWrapper.GetValue<std::string>("messageType", ""), "startProvisioningResponse");
    EXPECT_EQ(jsonWrapper.GetValue<std::string>("nodeId", ""), "testNode");
    EXPECT_TRUE(jsonWrapper.Has("csrs"));
    EXPECT_TRUE(jsonWrapper.Has("errorInfo"));

    auto parsedResponse = std::make_unique<aos::cloudprotocol::StartProvisioningResponse>();
    ASSERT_EQ(FromJSON(jsonWrapper, *parsedResponse), ErrorEnum::eNone);

    ASSERT_EQ(*response, *parsedResponse);
}

TEST_F(CloudProtocolProvisioning, EmptyFinishProvisioningRequest)
{
    auto request = std::make_unique<aos::cloudprotocol::FinishProvisioningRequest>();

    auto json = Poco::makeShared<Poco::JSON::Object>();
    ASSERT_EQ(ToJSON(*request, *json), ErrorEnum::eNone);

    utils::CaseInsensitiveObjectWrapper jsonWrapper(json);

    EXPECT_EQ(jsonWrapper.GetValue<std::string>("messageType", ""), "finishProvisioningRequest");
    EXPECT_EQ(jsonWrapper.GetValue<std::string>("nodeId", "empty expected"), "");
    EXPECT_TRUE(jsonWrapper.Has("certificates"));
    EXPECT_EQ(jsonWrapper.GetValue<std::string>("password", "empty expected"), "");

    auto parsedRequest = std::make_unique<aos::cloudprotocol::FinishProvisioningRequest>();
    ASSERT_EQ(FromJSON(jsonWrapper, *parsedRequest), ErrorEnum::eNone);

    ASSERT_EQ(*request, *parsedRequest);
}

TEST_F(CloudProtocolProvisioning, FinishProvisioningRequest)
{
    auto request = std::make_unique<aos::cloudprotocol::FinishProvisioningRequest>();
    request->mNodeID.Assign("testNode");
    request->mPassword.Assign("testPassword");

    request->mCertificates.EmplaceBack();
    request->mCertificates.Back().mNodeID.Assign("node-1");
    request->mCertificates.Back().mType = CertTypeEnum::eOffline;
    request->mCertificates.Back().mCertificateChain.Assign("cert-chain-1");

    request->mCertificates.EmplaceBack();
    request->mCertificates.Back().mNodeID.Assign("node-2");
    request->mCertificates.Back().mType = CertTypeEnum::eOnline;
    request->mCertificates.Back().mCertificateChain.Assign("cert-chain-2");

    request->mCertificates.EmplaceBack();

    auto json = Poco::makeShared<Poco::JSON::Object>();
    ASSERT_EQ(ToJSON(*request, *json), ErrorEnum::eNone);

    utils::CaseInsensitiveObjectWrapper jsonWrapper(json);

    EXPECT_EQ(jsonWrapper.GetValue<std::string>("messageType", ""), "finishProvisioningRequest");
    EXPECT_EQ(jsonWrapper.GetValue<std::string>("nodeId", ""), "testNode");
    EXPECT_TRUE(jsonWrapper.Has("certificates"));
    EXPECT_EQ(jsonWrapper.GetValue<std::string>("password", ""), "testPassword");

    auto parsedRequest = std::make_unique<aos::cloudprotocol::FinishProvisioningRequest>();
    ASSERT_EQ(FromJSON(jsonWrapper, *parsedRequest), ErrorEnum::eNone);

    ASSERT_EQ(*request, *parsedRequest);
}

TEST_F(CloudProtocolProvisioning, EmptyFinishProvisioningResponse)
{
    auto response = std::make_unique<aos::cloudprotocol::FinishProvisioningResponse>();

    auto json = Poco::makeShared<Poco::JSON::Object>();
    ASSERT_EQ(ToJSON(*response, *json), ErrorEnum::eNone);

    utils::CaseInsensitiveObjectWrapper jsonWrapper(json);

    EXPECT_EQ(jsonWrapper.GetValue<std::string>("messageType", ""), "finishProvisioningResponse");
    EXPECT_EQ(jsonWrapper.GetValue<std::string>("nodeId", "empty expected"), "");
    EXPECT_FALSE(jsonWrapper.Has("errorInfo"));

    auto parsedResponse = std::make_unique<aos::cloudprotocol::FinishProvisioningResponse>();
    ASSERT_EQ(FromJSON(jsonWrapper, *parsedResponse), ErrorEnum::eNone);

    ASSERT_EQ(*response, *parsedResponse);
}

TEST_F(CloudProtocolProvisioning, FinishProvisioningResponse)
{
    auto response = std::make_unique<aos::cloudprotocol::FinishProvisioningResponse>();
    response->mNodeID.Assign("testNode");
    response->mError = Error(ErrorEnum::eFailed, "Test error");

    auto json = Poco::makeShared<Poco::JSON::Object>();
    ASSERT_EQ(ToJSON(*response, *json), ErrorEnum::eNone);

    utils::CaseInsensitiveObjectWrapper jsonWrapper(json);

    EXPECT_EQ(jsonWrapper.GetValue<std::string>("messageType", ""), "finishProvisioningResponse");
    EXPECT_EQ(jsonWrapper.GetValue<std::string>("nodeId", ""), "testNode");
    EXPECT_TRUE(jsonWrapper.Has("errorInfo"));

    auto parsedResponse = std::make_unique<aos::cloudprotocol::FinishProvisioningResponse>();
    ASSERT_EQ(FromJSON(jsonWrapper, *parsedResponse), ErrorEnum::eNone);

    ASSERT_EQ(*response, *parsedResponse);
}

TEST_F(CloudProtocolProvisioning, EmptyDeprovisioningRequest)
{
    auto request = std::make_unique<aos::cloudprotocol::DeprovisioningRequest>();

    auto json = Poco::makeShared<Poco::JSON::Object>();
    ASSERT_EQ(ToJSON(*request, *json), ErrorEnum::eNone);

    utils::CaseInsensitiveObjectWrapper jsonWrapper(json);

    EXPECT_EQ(jsonWrapper.GetValue<std::string>("messageType", ""), "deprovisioningRequest");
    EXPECT_EQ(jsonWrapper.GetValue<std::string>("nodeId", "empty expected"), "");
    EXPECT_EQ(jsonWrapper.GetValue<std::string>("password", "empty expected"), "");

    auto parsedRequest = std::make_unique<aos::cloudprotocol::DeprovisioningRequest>();
    ASSERT_EQ(FromJSON(jsonWrapper, *parsedRequest), ErrorEnum::eNone);

    ASSERT_EQ(*request, *parsedRequest);
}

TEST_F(CloudProtocolProvisioning, DeprovisioningRequest)
{
    auto request = std::make_unique<aos::cloudprotocol::DeprovisioningRequest>();
    request->mNodeID.Assign("testNode");
    request->mPassword.Assign("testPassword");

    auto json = Poco::makeShared<Poco::JSON::Object>();
    ASSERT_EQ(ToJSON(*request, *json), ErrorEnum::eNone);

    utils::CaseInsensitiveObjectWrapper jsonWrapper(json);

    EXPECT_EQ(jsonWrapper.GetValue<std::string>("messageType", ""), "deprovisioningRequest");
    EXPECT_EQ(jsonWrapper.GetValue<std::string>("nodeId", ""), "testNode");
    EXPECT_EQ(jsonWrapper.GetValue<std::string>("password", ""), "testPassword");

    auto parsedRequest = std::make_unique<aos::cloudprotocol::DeprovisioningRequest>();
    ASSERT_EQ(FromJSON(jsonWrapper, *parsedRequest), ErrorEnum::eNone);

    ASSERT_EQ(*request, *parsedRequest);
}

TEST_F(CloudProtocolProvisioning, EmptyDeprovisioningResponse)
{
    auto response = std::make_unique<aos::cloudprotocol::DeprovisioningResponse>();

    auto json = Poco::makeShared<Poco::JSON::Object>();
    ASSERT_EQ(ToJSON(*response, *json), ErrorEnum::eNone);

    utils::CaseInsensitiveObjectWrapper jsonWrapper(json);

    EXPECT_EQ(jsonWrapper.GetValue<std::string>("messageType", ""), "deprovisioningResponse");
    EXPECT_EQ(jsonWrapper.GetValue<std::string>("nodeId", "empty expected"), "");
    EXPECT_FALSE(jsonWrapper.Has("errorInfo"));

    auto parsedResponse = std::make_unique<aos::cloudprotocol::DeprovisioningResponse>();
    ASSERT_EQ(FromJSON(jsonWrapper, *parsedResponse), ErrorEnum::eNone);

    ASSERT_EQ(*response, *parsedResponse);
}

TEST_F(CloudProtocolProvisioning, DeprovisioningResponse)
{
    auto response = std::make_unique<aos::cloudprotocol::DeprovisioningResponse>();
    response->mNodeID.Assign("testNode");
    response->mError = Error(ErrorEnum::eFailed, "Test error");

    auto json = Poco::makeShared<Poco::JSON::Object>();
    ASSERT_EQ(ToJSON(*response, *json), ErrorEnum::eNone);

    utils::CaseInsensitiveObjectWrapper jsonWrapper(json);

    EXPECT_EQ(jsonWrapper.GetValue<std::string>("messageType", ""), "deprovisioningResponse");
    EXPECT_EQ(jsonWrapper.GetValue<std::string>("nodeId", ""), "testNode");
    EXPECT_TRUE(jsonWrapper.Has("errorInfo"));

    auto parsedResponse = std::make_unique<aos::cloudprotocol::DeprovisioningResponse>();
    ASSERT_EQ(FromJSON(jsonWrapper, *parsedResponse), ErrorEnum::eNone);

    ASSERT_EQ(*response, *parsedResponse);
}

} // namespace aos::common::cloudprotocol
