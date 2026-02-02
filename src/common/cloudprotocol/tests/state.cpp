/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <array>

#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>

#include <common/cloudprotocol/state.hpp>

using namespace testing;

namespace aos::common::cloudprotocol {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

StaticArray<uint8_t, crypto::cSHA256Size> ToByteArray(const String& str)
{
    StaticArray<uint8_t, crypto::cSHA256Size> result;

    auto err = str.HexToByteArray(result);
    EXPECT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    return result;
}

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class CloudProtocolState : public Test {
public:
    void SetUp() override { tests::utils::InitLog(); }
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(CloudProtocolState, StateAcceptance)
{
    const auto cJSON = R"({
        "item": {
            "id": "item1"
        },
        "subject": {
            "id": "subject1"
        },
        "correlationId": "correlation1",
        "instance": "10",
        "checksum": "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08",
        "result": "accepted",
        "reason": "All good"
    })";

    auto [jsonVar, err] = common::utils::ParseJson(cJSON);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    auto wrapper = common::utils::CaseInsensitiveObjectWrapper(jsonVar);

    auto state = std::make_unique<StateAcceptance>();

    err = FromJSON(wrapper, *state);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(state->mCorrelationID, "correlation1");
    EXPECT_EQ(state->mItemID, "item1");
    EXPECT_EQ(state->mSubjectID, "subject1");
    EXPECT_EQ(state->mInstance, 10);
    EXPECT_EQ(state->mChecksum, ToByteArray("9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08"));
    EXPECT_EQ(state->mResult.GetValue(), StateResultEnum::eAccepted);
    EXPECT_STREQ(state->mReason.CStr(), "All good");
}

TEST_F(CloudProtocolState, UpdateState)
{
    const auto cJSON = R"({
        "item": {
            "id": "item1"
        },
        "subject": {
            "id": "subject1"
        },
        "correlationId": "correlation1",
        "instance": "10",
        "stateChecksum": "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08",
        "state": "test"
    })";

    auto [jsonVar, err] = common::utils::ParseJson(cJSON);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    auto wrapper = common::utils::CaseInsensitiveObjectWrapper(jsonVar);

    auto state = std::make_unique<UpdateState>();

    err = FromJSON(wrapper, *state);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(state->mCorrelationID, "correlation1");
    EXPECT_EQ(state->mItemID, "item1");
    EXPECT_EQ(state->mSubjectID, "subject1");
    EXPECT_EQ(state->mInstance, 10);
    EXPECT_STREQ(state->mState.CStr(), "test");
    EXPECT_EQ(state->mChecksum, ToByteArray("9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08"));
}

TEST_F(CloudProtocolState, NewState)
{
    const auto cExpectedJSON
        = R"({"messageType":"newState","correlationId":"correlation1","item":{"id":"item1"},"subject":{"id":"subject1"},)"
          R"("instance":10,"stateChecksum":"9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08",)"
          R"("state":"test"})";

    auto state            = std::make_unique<NewState>();
    state->mCorrelationID = "correlation1";
    state->mItemID        = "item1";
    state->mSubjectID     = "subject1";
    state->mInstance      = 10;
    state->mChecksum      = ToByteArray("9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08");
    state->mState         = "test";

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(*state, *json);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(common::utils::Stringify(json), cExpectedJSON);
}

TEST_F(CloudProtocolState, StateRequest)
{
    const auto cExpectedJSON
        = R"({"messageType":"stateRequest","correlationId":"correlation1","item":{"id":"item1"},"subject":{"id":"subject1"},)"
          R"("instance":10,"default":true})";

    auto state            = std::make_unique<StateRequest>();
    state->mCorrelationID = "correlation1";
    state->mItemID        = "item1";
    state->mSubjectID     = "subject1";
    state->mInstance      = 10;
    state->mDefault       = true;

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(*state, *json);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(common::utils::Stringify(json), cExpectedJSON);
}

} // namespace aos::common::cloudprotocol
