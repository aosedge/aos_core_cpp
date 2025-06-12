/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024s EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include "visidentifier/vismessage.hpp"

namespace aos::iam::visidentifier {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

class VISMessageTest : public testing::Test { };

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(VISMessageTest, ConstructFromJson)
{
    Poco::JSON::Object json;

    json.set("action", "get");
    json.set("path", "test-path");
    json.set("requestId", "test-request-id");

    std::ostringstream jsonStream;
    Poco::JSON::Stringifier::stringify(json, jsonStream);

    VISMessage message(jsonStream.str());

    EXPECT_EQ(json.size(), message.GetJSON().size());

    EXPECT_EQ(message.GetValue<std::string>(VISMessage::cActionTagName), "get");
    EXPECT_EQ(message.GetValue<std::string>(VISMessage::cPathTagName), "test-path");
    EXPECT_EQ(message.GetValue<std::string>(VISMessage::cRequestIdTagName), "test-request-id");
}

TEST_F(VISMessageTest, IsSucceeds)
{
    const VISMessage message(VISActionEnum::eGet);

    EXPECT_TRUE(message.Is(VISActionEnum::eGet));
}

TEST_F(VISMessageTest, IsFails)
{
    const VISMessage message(VISActionEnum::eGet);

    EXPECT_FALSE(message.Is(VISActionEnum::eSubscribe));
}

TEST_F(VISMessageTest, ConstructorSetsAction)
{
    const VISMessage message(VISActionEnum::eGet);

    std::string actionTagValue;

    ASSERT_NO_THROW(actionTagValue = message.GetValue<std::string>(VISMessage::cActionTagName));
    EXPECT_EQ(actionTagValue, "get");
}

TEST_F(VISMessageTest, SetKeyValue)
{
    VISMessage message(VISActionEnum::eGet);

    message.SetKeyValue("key", "value");
    EXPECT_EQ(message.GetValue<std::string>("key"), "value");

    message.SetKeyValue("key", "value1");
    EXPECT_EQ(message.GetValue<std::string>("key"), "value1");

    message.SetKeyValue("key", 10);
    EXPECT_EQ(message.GetValue<int>("key"), 10);
}

TEST_F(VISMessageTest, GetValueThrowsOnKeyNotFound)
{
    VISMessage message(VISActionEnum::eGet);

    ASSERT_THROW(message.GetValue<std::string>("key-not-found"), Poco::Exception);
}

TEST_F(VISMessageTest, GetValueThrowsOnInvalidGetType)
{
    VISMessage message(VISActionEnum::eGet);

    message.SetKeyValue("key", "str10");

    ASSERT_THROW(message.GetValue<int>("key"), Poco::Exception);
}

} // namespace aos::iam::visidentifier
