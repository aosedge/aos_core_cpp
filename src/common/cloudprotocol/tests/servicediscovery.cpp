/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <array>

#include <gtest/gtest.h>

#include <aos/test/log.hpp>
#include <aos/test/utils.hpp>

#include <common/cloudprotocol/servicediscovery.hpp>
#include <common/utils/json.hpp>

using namespace testing;

namespace aos::common::cloudprotocol {

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class CloudProtocolServiceDiscovery : public Test {
public:
    void SetUp() override { test::InitLog(); }
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(CloudProtocolServiceDiscovery, DiscoveryRequest)
{
    auto request      = std::make_unique<aos::cloudprotocol::ServiceDiscoveryRequest>();
    request->mVersion = 1;
    request->mSystemID.Assign("test-system-id");
    request->mSupportedProtocols.EmplaceBack("wss");

    auto json = Poco::makeShared<Poco::JSON::Object>();

    ASSERT_EQ(ToJSON(*request, *json), Error(ErrorEnum::eNone));

    auto jsonWrapper = utils::CaseInsensitiveObjectWrapper(json);

    EXPECT_EQ(jsonWrapper.GetValue<size_t>("version"), 1);
    EXPECT_EQ(jsonWrapper.GetValue<std::string>("systemId"), "test-system-id");
    EXPECT_EQ(jsonWrapper.GetArray("supportedProtocols")->size(), 1);
    EXPECT_EQ(jsonWrapper.GetArray("supportedProtocols")->getElement<std::string>(0), "wss");

    auto parsedRequest = std::make_unique<aos::cloudprotocol::ServiceDiscoveryRequest>();

    ASSERT_EQ(FromJSON(jsonWrapper, *parsedRequest), Error(ErrorEnum::eNone));

    EXPECT_EQ(*request, *parsedRequest);
}

TEST_F(CloudProtocolServiceDiscovery, DiscoveryResponse)
{
    auto response      = std::make_unique<aos::cloudprotocol::ServiceDiscoveryResponse>();
    response->mVersion = 1;
    response->mSystemID.Assign("test-system-id");
    response->mNextRequestDelay = Time::cMilliseconds * 30;
    response->mConnectionInfo.EmplaceBack("wss://example.com");
    response->mConnectionInfo.EmplaceBack("https://example.com");
    response->mConnectionInfo.EmplaceBack("http://example.com");
    response->mAuthToken.Assign("test-auth-token");
    response->mErrorCode = aos::cloudprotocol::ServiceDiscoveryResponseErrorEnum::eRedirect;

    auto json = Poco::makeShared<Poco::JSON::Object>();

    ASSERT_EQ(ToJSON(*response, *json), Error(ErrorEnum::eNone));

    auto jsonWrapper = utils::CaseInsensitiveObjectWrapper(json);

    EXPECT_EQ(jsonWrapper.GetValue<size_t>("version"), 1);
    EXPECT_EQ(jsonWrapper.GetValue<std::string>("systemId"), "test-system-id");
    EXPECT_EQ(jsonWrapper.GetValue<int32_t>("nextRequestDelay"), 30);
    EXPECT_EQ(jsonWrapper.GetArray("connectionInfo")->size(), 3);
    EXPECT_EQ(jsonWrapper.GetArray("connectionInfo")->getElement<std::string>(0), "wss://example.com");
    EXPECT_EQ(jsonWrapper.GetArray("connectionInfo")->getElement<std::string>(1), "https://example.com");
    EXPECT_EQ(jsonWrapper.GetArray("connectionInfo")->getElement<std::string>(2), "http://example.com");
    EXPECT_EQ(jsonWrapper.GetValue<std::string>("authToken"), "test-auth-token");
    EXPECT_EQ(jsonWrapper.GetValue<size_t>("errorCode"), 1);

    auto parsedResponse = std::make_unique<aos::cloudprotocol::ServiceDiscoveryResponse>();

    ASSERT_EQ(FromJSON(jsonWrapper, *parsedResponse), Error(ErrorEnum::eNone));

    EXPECT_EQ(*response, *parsedResponse);
}

} // namespace aos::common::cloudprotocol
