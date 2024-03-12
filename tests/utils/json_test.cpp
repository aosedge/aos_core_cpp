/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024s EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <fstream>

#include <Poco/JSON/Object.h>
#include <gtest/gtest.h>

#include "logger/logger.hpp"
#include "utils/json.hpp"

using namespace testing;

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

class JsonTest : public Test { };

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

namespace UtilsJson {

TEST_F(JsonTest, ParseJsonSucceedsFromString)
{
    aos::Error         err;
    Poco::Dynamic::Var result;

    ASSERT_NO_THROW(aos::Tie(result, err) = ParseJson(R"({"key":"value"})"));
    EXPECT_EQ(result.type(), typeid(Poco::JSON::Object::Ptr));
}

TEST_F(JsonTest, ParseJsonSucceedsFromStream)
{
    aos::Error         err;
    Poco::Dynamic::Var result;
    std::istringstream in(R"({"key": "value"})");

    ASSERT_TRUE(in.good());

    ASSERT_NO_THROW(aos::Tie(result, err) = ParseJson(in));
    EXPECT_EQ(result.type(), typeid(Poco::JSON::Object::Ptr));
}

TEST_F(JsonTest, ParseJsonFailsFromString)
{
    aos::Error         err;
    Poco::Dynamic::Var result;

    ASSERT_NO_THROW(aos::Tie(result, err) = ParseJson(""));
    EXPECT_TRUE(err.Is(aos::ErrorEnum::eInvalidArgument));
    EXPECT_TRUE(result.isEmpty());
}

TEST_F(JsonTest, ParseJsonFailsFromStream)
{
    aos::Error         err;
    Poco::Dynamic::Var result;
    std::ifstream      in;

    ASSERT_NO_THROW(aos::Tie(result, err) = ParseJson(in));
    EXPECT_TRUE(err.Is(aos::ErrorEnum::eInvalidArgument));
    EXPECT_TRUE(result.isEmpty());
}

TEST_F(JsonTest, FindByPathSucceeds)
{
    Poco::JSON::Object object;
    object.set("key", "value");

    auto res = FindByPath(object, std::vector<std::string> {"key"});
    EXPECT_TRUE(res.isString());
    EXPECT_EQ(res.extract<std::string>(), "value");

    Poco::JSON::Object::Ptr objectPtr = new Poco::JSON::Object(object);

    res = FindByPath(objectPtr, std::vector<std::string> {"key"});
    EXPECT_TRUE(res.isString());
    EXPECT_EQ(res.extract<std::string>(), "value");
}

TEST_F(JsonTest, FindByPathSucceedsEmptyPath)
{
    Poco::JSON::Object object;
    object.set("key", "value");

    auto res = FindByPath(object, {});
    EXPECT_FALSE(res.isEmpty());
    EXPECT_EQ(res.type(), typeid(object));
}

TEST_F(JsonTest, FindByPathSucceedsOnNestedJson)
{
    Poco::JSON::Object value;
    value.set("key", "value");
    value.set("aos.key", "aos.value");

    Poco::JSON::Object object;
    object.set("data", value);

    auto res = FindByPath(object, {"data", "aos.key"});
    ASSERT_TRUE(res.isString());
    EXPECT_EQ(res.extract<std::string>(), "aos.value");

    res = FindByPath(object, {"data", "key"});
    ASSERT_TRUE(res.isString());
    EXPECT_EQ(res.extract<std::string>(), "value");
}

TEST_F(JsonTest, FindByPathFails)
{
    Poco::JSON::Object value;
    value.set("key", "value");

    Poco::JSON::Object object;
    object.set("data", value);

    auto res = FindByPath(object, {"key"});
    EXPECT_TRUE(res.isEmpty());

    res = FindByPath(object, {"data", "key", "doesnt-exist"});
    EXPECT_TRUE(res.isEmpty());

    res = FindByPath(Poco::Dynamic::Var(), {"data", "key", "doesnt-exist"});
    EXPECT_TRUE(res.isEmpty());
}

} // namespace UtilsJson
