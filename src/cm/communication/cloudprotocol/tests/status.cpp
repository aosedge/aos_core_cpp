/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>

#include <cm/communication/cloudprotocol/status.hpp>

using namespace testing;

namespace aos::cm::communication::cloudprotocol {

namespace {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

} // namespace

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class CloudProtocolStatus : public Test {
public:
    void SetUp() override { tests::utils::InitLog(); }
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(CloudProtocolStatus, AckToJSON)
{
    constexpr auto cExpectedMessage = R"({"messageType":"ack","correlationID":"id"})";

    auto ack            = std::make_unique<Ack>();
    ack->mCorrelationID = "id";

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(*ack, *json);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(common::utils::Stringify(json), cExpectedMessage);
}

TEST_F(CloudProtocolStatus, AckFromJSON)
{
    constexpr auto cJSON = R"({"messageType":"ack","correlationID":"id"})";

    auto [jsonVar, err] = common::utils::ParseJson(cJSON);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    auto wrapper = common::utils::CaseInsensitiveObjectWrapper(jsonVar);

    auto ack = std::make_unique<Ack>();

    err = FromJSON(wrapper, *ack);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(ack->mCorrelationID, "id");
}

TEST_F(CloudProtocolStatus, NackToJSON)
{
    constexpr auto cExpectedMessage = R"({"messageType":"nack","correlationID":"id","retryAfter":100})";

    auto nack            = std::make_unique<Nack>();
    nack->mCorrelationID = "id";
    nack->mRetryAfter    = Time::cMilliseconds * 100;

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(*nack, *json);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(common::utils::Stringify(json), cExpectedMessage);
}

TEST_F(CloudProtocolStatus, NackFromJSONUsesDefaultRetryAfter)
{
    constexpr auto cJSON = R"({"messageType":"nack","correlationID":"id"})";

    auto [jsonVar, err] = common::utils::ParseJson(cJSON);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    auto wrapper = common::utils::CaseInsensitiveObjectWrapper(jsonVar);

    auto nack = std::make_unique<Nack>();

    err = FromJSON(wrapper, *nack);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(nack->mCorrelationID, "id");
    EXPECT_EQ(nack->mRetryAfter, 500 * Time::cMilliseconds);
}

TEST_F(CloudProtocolStatus, NackFromJSONCustomRetryAfter)
{
    constexpr auto cJSON = R"({"messageType":"nack","correlationID":"id","retryAfter":224})";

    auto [jsonVar, err] = common::utils::ParseJson(cJSON);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    auto wrapper = common::utils::CaseInsensitiveObjectWrapper(jsonVar);

    auto nack = std::make_unique<Nack>();

    err = FromJSON(wrapper, *nack);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(nack->mCorrelationID, "id");
    EXPECT_EQ(nack->mRetryAfter, 224 * Time::cMilliseconds);
}

} // namespace aos::cm::communication::cloudprotocol
