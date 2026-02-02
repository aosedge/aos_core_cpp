/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>

#include <common/cloudprotocol/servicediscovery.hpp>
#include <common/utils/json.hpp>

using namespace testing;

namespace aos::common::cloudprotocol {

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class CloudProtocolServiceDiscovery : public Test {
public:
    void SetUp() override { tests::utils::InitLog(); }
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(CloudProtocolServiceDiscovery, DiscoveryRequest)
{
    constexpr auto cJSON = R"({"version":1,"systemId":"test-system-id","supportedProtocols":["wss"]})";

    ServiceDiscoveryRequest request;

    request.mVersion  = 1;
    request.mSystemID = "test-system-id";
    request.mSupportedProtocols.emplace_back("wss");

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(request, *json);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(common::utils::Stringify(json), cJSON);
}

TEST_F(CloudProtocolServiceDiscovery, DiscoveryResponse)
{
    constexpr auto cJSON = R"({
        "version": 1,
        "systemId": "test-system-id",
        "nextRequestDelay": 30,
        "connectionInfo": [
            "wss://example.com",
            "https://example.com",
            "http://example.com"
        ],
        "authToken": "test-auth-token",
        "errorCode": 1
    })";

    ServiceDiscoveryResponse response;

    auto err = FromJSON(cJSON, response);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(response.mVersion, 1);
    EXPECT_EQ(response.mSystemID, "test-system-id");
    EXPECT_EQ(response.mNextRequestDelay.Milliseconds(), 30);
    ASSERT_EQ(response.mConnectionInfo.size(), 3u);

    EXPECT_EQ(response.mConnectionInfo[0], "wss://example.com");
    EXPECT_EQ(response.mConnectionInfo[1], "https://example.com");
    EXPECT_EQ(response.mConnectionInfo[2], "http://example.com");

    EXPECT_EQ(response.mAuthToken, "test-auth-token");
    EXPECT_EQ(response.mErrorCode.GetValue(), ServiceDiscoveryResponseErrorEnum::eRedirect);
}

} // namespace aos::common::cloudprotocol
