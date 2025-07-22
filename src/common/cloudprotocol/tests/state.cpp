/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <array>

#include <gtest/gtest.h>

#include <aos/test/log.hpp>
#include <aos/test/utils.hpp>

#include <common/cloudprotocol/state.hpp>
#include <common/utils/json.hpp>

using namespace testing;

namespace aos::common::cloudprotocol {

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class CloudProtocolState : public Test {
public:
    void SetUp() override { test::InitLog(); }
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(CloudProtocolState, StateAcceptance)
{
    aos::cloudprotocol::StateAcceptance state({"service1", "subject1", 42});
    state.mChecksum = "test_checksum";
    state.mResult   = aos::cloudprotocol::StateResultEnum::eRejected;
    state.mReason   = "test reason";

    auto json = Poco::makeShared<Poco::JSON::Object>();
    ASSERT_EQ(ToJSON(state, *json), ErrorEnum::eNone);

    utils::CaseInsensitiveObjectWrapper jsonWrapper(json);

    EXPECT_EQ(jsonWrapper.GetValue<std::string>("messageType", ""), "stateAcceptance");
    EXPECT_EQ(jsonWrapper.GetValue<std::string>("serviceID", ""), "service1");
    EXPECT_EQ(jsonWrapper.GetValue<std::string>("subjectID", ""), "subject1");
    EXPECT_EQ(jsonWrapper.GetValue<int>("instance", -1), 42);
    EXPECT_EQ(jsonWrapper.GetValue<std::string>("checksum", ""), "test_checksum");
    EXPECT_EQ(jsonWrapper.GetValue<std::string>("result", ""), "rejected");
    EXPECT_EQ(jsonWrapper.GetValue<std::string>("reason", ""), "test reason");

    aos::cloudprotocol::StateAcceptance parsedState;
    ASSERT_EQ(FromJSON(jsonWrapper, parsedState), ErrorEnum::eNone);

    ASSERT_EQ(state, parsedState);
}

TEST_F(CloudProtocolState, UpdateState)
{
    auto state       = std::make_unique<aos::cloudprotocol::UpdateState>(InstanceIdent {"service1", "subject1", 42});
    state->mChecksum = "test_checksum";
    state->mState    = "test state";

    auto json = Poco::makeShared<Poco::JSON::Object>();
    ASSERT_EQ(ToJSON(*state, *json), ErrorEnum::eNone);

    utils::CaseInsensitiveObjectWrapper jsonWrapper(json);

    EXPECT_EQ(jsonWrapper.GetValue<std::string>("messageType", ""), "updateState");
    EXPECT_EQ(jsonWrapper.GetValue<std::string>("serviceID", ""), "service1");
    EXPECT_EQ(jsonWrapper.GetValue<std::string>("subjectID", ""), "subject1");
    EXPECT_EQ(jsonWrapper.GetValue<int>("instance", -1), 42);
    EXPECT_EQ(jsonWrapper.GetValue<std::string>("stateChecksum", ""), "test_checksum");
    EXPECT_EQ(jsonWrapper.GetValue<std::string>("state", ""), "test state");

    auto parsedState = std::make_unique<aos::cloudprotocol::UpdateState>();
    ASSERT_EQ(FromJSON(jsonWrapper, *parsedState), ErrorEnum::eNone);

    ASSERT_EQ(*state, *parsedState);
}

TEST_F(CloudProtocolState, NewState)
{
    auto state       = std::make_unique<aos::cloudprotocol::NewState>(InstanceIdent {"service1", "subject1", 42});
    state->mChecksum = "test_checksum";
    state->mState    = "test state";

    auto json = Poco::makeShared<Poco::JSON::Object>();
    ASSERT_EQ(ToJSON(*state, *json), ErrorEnum::eNone);

    utils::CaseInsensitiveObjectWrapper jsonWrapper(json);

    EXPECT_EQ(jsonWrapper.GetValue<std::string>("messageType", ""), "newState");
    EXPECT_EQ(jsonWrapper.GetValue<std::string>("serviceID", ""), "service1");
    EXPECT_EQ(jsonWrapper.GetValue<std::string>("subjectID", ""), "subject1");
    EXPECT_EQ(jsonWrapper.GetValue<int>("instance", -1), 42);
    EXPECT_EQ(jsonWrapper.GetValue<std::string>("stateChecksum", ""), "test_checksum");
    EXPECT_EQ(jsonWrapper.GetValue<std::string>("state", ""), "test state");

    auto parsedState = std::make_unique<aos::cloudprotocol::NewState>();
    ASSERT_EQ(FromJSON(jsonWrapper, *parsedState), ErrorEnum::eNone);

    ASSERT_EQ(*state, *parsedState);
}

TEST_F(CloudProtocolState, StateRequest)
{
    auto state      = std::make_unique<aos::cloudprotocol::StateRequest>(InstanceIdent {"service1", "subject1", 42});
    state->mDefault = true;

    auto json = Poco::makeShared<Poco::JSON::Object>();
    ASSERT_EQ(ToJSON(*state, *json), ErrorEnum::eNone);

    utils::CaseInsensitiveObjectWrapper jsonWrapper(json);

    EXPECT_EQ(jsonWrapper.GetValue<std::string>("messageType", ""), "stateRequest");
    EXPECT_EQ(jsonWrapper.GetValue<std::string>("serviceID", ""), "service1");
    EXPECT_EQ(jsonWrapper.GetValue<std::string>("subjectID", ""), "subject1");
    EXPECT_EQ(jsonWrapper.GetValue<int>("instance", -1), 42);
    EXPECT_EQ(jsonWrapper.GetValue<bool>("default", false), true);

    auto parsedState = std::make_unique<aos::cloudprotocol::StateRequest>();
    ASSERT_EQ(FromJSON(jsonWrapper, *parsedState), ErrorEnum::eNone);

    ASSERT_EQ(*state, *parsedState);
}

} // namespace aos::common::cloudprotocol
