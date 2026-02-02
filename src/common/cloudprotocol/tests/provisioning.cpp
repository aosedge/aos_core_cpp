/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>

#include <common/cloudprotocol/provisioning.hpp>

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

TEST_F(CloudProtocolProvisioning, StartProvisioningRequest)
{
    const auto cJSON = R"({
        "correlationId": "id",
        "node": {
            "codename": "node1"
        },
        "password": "test_password"
    })";

    auto [jsonVar, err] = common::utils::ParseJson(cJSON);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    auto wrapper = common::utils::CaseInsensitiveObjectWrapper(jsonVar);

    auto request = std::make_unique<StartProvisioningRequest>();

    err = FromJSON(wrapper, *request);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(request->mCorrelationID, "id");
    EXPECT_EQ(request->mNodeID, "node1");
    EXPECT_EQ(request->mPassword, "test_password");
}

TEST_F(CloudProtocolProvisioning, StartProvisioningResponseWithoutError)
{
    constexpr auto cJSON = R"({"messageType":"startProvisioningResponse","correlationId":"id",)"
                           R"("node":{"codename":"node1"},)"
                           R"("csrs":[{"type":"cm","csr":"cm scr"},)"
                           R"({"type":"iam","csr":"iam csr"}]})";

    auto response            = std::make_unique<StartProvisioningResponse>();
    response->mCorrelationID = "id";
    response->mNodeID        = "node1";

    response->mCSRs.EmplaceBack();
    response->mCSRs.Back().mType = CertTypeEnum::eCM;
    response->mCSRs.Back().mCSR  = "cm scr";

    response->mCSRs.EmplaceBack();
    response->mCSRs.Back().mType = CertTypeEnum::eIAM;
    response->mCSRs.Back().mCSR  = "iam csr";

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(*response, *json);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(common::utils::Stringify(json), cJSON);
}

TEST_F(CloudProtocolProvisioning, StartProvisioningResponseWithError)
{
    constexpr auto cJSON = R"({"messageType":"startProvisioningResponse","correlationId":"id",)"
                           R"("node":{"codename":"node1"},"errorInfo":)"
                           R"({"aosCode":1,"exitCode":0,"message":""},"csrs":[{"type":"cm","csr":"cm scr"},)"
                           R"({"type":"iam","csr":"iam csr"}]})";

    auto response            = std::make_unique<StartProvisioningResponse>();
    response->mCorrelationID = "id";
    response->mNodeID        = "node1";

    response->mCSRs.EmplaceBack();
    response->mCSRs.Back().mType = CertTypeEnum::eCM;
    response->mCSRs.Back().mCSR  = "cm scr";

    response->mCSRs.EmplaceBack();
    response->mCSRs.Back().mType = CertTypeEnum::eIAM;
    response->mCSRs.Back().mCSR  = "iam csr";

    response->mError = ErrorEnum::eFailed;

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(*response, *json);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(common::utils::Stringify(json), cJSON);
}

TEST_F(CloudProtocolProvisioning, FinishProvisioningRequest)
{
    const auto cJSON = R"({
        "correlationId": "id",
        "node": {
            "codename": "node1"
        },
        "certificates": [
            {
                "type": "cm",
                "chain": "cm chain"
            },
            {
                "type": "iam",
                "chain": "iam chain"
            }
        ],
        "password": "test_password"
    })";

    auto [jsonVar, err] = common::utils::ParseJson(cJSON);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    auto wrapper = common::utils::CaseInsensitiveObjectWrapper(jsonVar);

    auto request = std::make_unique<FinishProvisioningRequest>();

    err = FromJSON(wrapper, *request);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(request->mCorrelationID, "id");

    EXPECT_EQ(request->mNodeID, "node1");
    ASSERT_EQ(request->mCertificates.Size(), 2);

    EXPECT_EQ(request->mCertificates[0].mCertType.GetValue(), CertTypeEnum::eCM);
    EXPECT_EQ(request->mCertificates[0].mCertChain, "cm chain");

    EXPECT_EQ(request->mCertificates[1].mCertType.GetValue(), CertTypeEnum::eIAM);
    EXPECT_EQ(request->mCertificates[1].mCertChain, "iam chain");

    EXPECT_EQ(request->mPassword, "test_password");
}

TEST_F(CloudProtocolProvisioning, FinishProvisioningResponseWithoutError)
{
    constexpr auto cJSON
        = R"({"messageType":"finishProvisioningResponse","correlationId":"id","node":{"codename":"node1"}})";

    auto response            = std::make_unique<FinishProvisioningResponse>();
    response->mCorrelationID = "id";
    response->mNodeID        = "node1";

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(*response, *json);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(common::utils::Stringify(json), cJSON);
}

TEST_F(CloudProtocolProvisioning, FinishProvisioningResponseWithError)
{
    constexpr auto cJSON = R"({"messageType":"finishProvisioningResponse","correlationId":"id",)"
                           R"("node":{"codename":"node1"},"errorInfo":)"
                           R"({"aosCode":1,"exitCode":0,"message":""}})";

    auto response            = std::make_unique<FinishProvisioningResponse>();
    response->mCorrelationID = "id";
    response->mNodeID        = "node1";
    response->mError         = ErrorEnum::eFailed;

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(*response, *json);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(common::utils::Stringify(json), cJSON);
}

TEST_F(CloudProtocolProvisioning, DeprovisioningRequest)
{
    const auto cJSON = R"({
        "correlationId": "id",
        "node": {
            "codename": "node1"
        },
        "password": "test_password"
    })";

    auto [jsonVar, err] = common::utils::ParseJson(cJSON);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    auto wrapper = common::utils::CaseInsensitiveObjectWrapper(jsonVar);

    auto request = std::make_unique<DeprovisioningRequest>();

    err = FromJSON(wrapper, *request);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(request->mCorrelationID, "id");
    EXPECT_EQ(request->mNodeID, "node1");
    EXPECT_EQ(request->mPassword, "test_password");
}

TEST_F(CloudProtocolProvisioning, DeprovisioningResponseWithoutError)
{
    constexpr auto cJSON
        = R"({"messageType":"deprovisioningResponse","correlationId":"id","node":{"codename":"node1"}})";

    auto response            = std::make_unique<DeprovisioningResponse>();
    response->mCorrelationID = "id";
    response->mNodeID        = "node1";

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(*response, *json);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(common::utils::Stringify(json), cJSON);
}

TEST_F(CloudProtocolProvisioning, DeprovisioningResponseWithError)
{
    constexpr auto cJSON = R"({"messageType":"deprovisioningResponse","correlationId":"id",)"
                           R"("node":{"codename":"node1"},"errorInfo":)"
                           R"({"aosCode":1,"exitCode":0,"message":""}})";

    auto response            = std::make_unique<DeprovisioningResponse>();
    response->mCorrelationID = "id";
    response->mNodeID        = "node1";
    response->mError         = ErrorEnum::eFailed;

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(*response, *json);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(common::utils::Stringify(json), cJSON);
}

} // namespace aos::common::cloudprotocol
