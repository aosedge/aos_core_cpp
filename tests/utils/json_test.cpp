/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024s EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <fstream>

#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>

#include <gtest/gtest.h>

#include "utils/grpchelper.hpp"
#include "utils/json.hpp"

using namespace testing;

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

class JsonTest : public Test { };

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

namespace aos::common::utils {

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

TEST_F(JsonTest, CaseInsensitiveObjectWrapperSucceeds)
{
    Poco::JSON::Object::Ptr object = new Poco::JSON::Object();
    object->set("Key", "value");

    Poco::JSON::Array::Ptr array = new Poco::JSON::Array();
    array->add("value1");
    array->add("value2");
    object->set("Array", array);

    CaseInsensitiveObjectWrapper wrapper(object);

    EXPECT_TRUE(wrapper.Has("key"));
    EXPECT_EQ(wrapper.Get("key").convert<std::string>(), "value");
    EXPECT_EQ(wrapper.GetValue<std::string>("key", "default"), "value");

    auto arrayValue = GetArrayValue<std::string>(
        wrapper, "array", [](const Poco::Dynamic::Var& value) { return value.convert<std::string>(); });

    ASSERT_EQ(arrayValue.size(), 2);
    EXPECT_EQ(arrayValue[0], "value1");
    EXPECT_EQ(arrayValue[1], "value2");

    EXPECT_FALSE(wrapper.GetOptionalValue<std::string>("non-exist").has_value());
    EXPECT_EQ(wrapper.GetOptionalValue<std::string>("key").value(), "value");
}

TEST_F(JsonTest, CaseInsensitiveObjectWrapperFromPocoVarSucceeds)
{
    try {
        Poco::JSON::Parser parser;
        auto               result = parser.parse({R"({"Key":"value","Array":["value1","value2"]})"});

        CaseInsensitiveObjectWrapper wrapper(result);

        EXPECT_TRUE(wrapper.Has("key"));
        EXPECT_EQ(wrapper.GetValue<std::string>("key"), "value");
    } catch (const Poco::Exception& e) {
        FAIL() << e.displayText();
    }
}

TEST_F(JsonTest, CaseInsensitiveObjectWrapperFromPocoVarFails)
{
    try {
        Poco::JSON::Parser parser;
        auto               result = parser.parse({R"(["value1","value2"])"});

        ASSERT_THROW(CaseInsensitiveObjectWrapper {result}, Poco::BadCastException);
    } catch (const Poco::Exception& e) {
        FAIL() << e.displayText();
    }
}

TEST_F(JsonTest, ParseValueArraySucceeds)
{
    try {
        Poco::JSON::Object::Ptr object      = new Poco::JSON::Object();
        Poco::JSON::Array::Ptr  numberArray = new Poco::JSON::Array();
        Poco::JSON::Array::Ptr  stringArray = new Poco::JSON::Array();

        const std::vector<uint32_t>    expectedNumbers = {1, 2, 3, 4};
        const std::vector<std::string> expectedStrings = {"value1", "value2", "value3"};

        for (const auto number : expectedNumbers) {
            numberArray->add(number);
        }

        for (const auto& string : expectedStrings) {
            stringArray->add(string);
        }

        object->set("numbers", numberArray);
        object->set("strings", stringArray);

        CaseInsensitiveObjectWrapper wrapper(object);

        const auto numbers = GetArrayValue<uint32_t>(wrapper, "numbers");

        ASSERT_EQ(numbers.size(), expectedNumbers.size());
        for (size_t i = 0; i < numbers.size(); ++i) {
            EXPECT_EQ(numbers[i], expectedNumbers[i]);
        }

        const auto strings = GetArrayValue<std::string>(wrapper, "strings");

        ASSERT_EQ(strings.size(), expectedStrings.size());
        for (size_t i = 0; i < strings.size(); ++i) {
            EXPECT_EQ(strings[i], expectedStrings[i]);
        }
    } catch (const Poco::Exception& e) {
        FAIL() << e.displayText();
    }
}

TEST_F(JsonTest, WriteJsonToFileSucceeds)
{
    Poco::JSON::Object::Ptr object = new Poco::JSON::Object();
    object->set("key", "value");

    std::string path = "test.json";

    EXPECT_EQ(WriteJsonToFile(object, path), aos::ErrorEnum::eNone);

    std::ifstream file(path);
    std::string   content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    EXPECT_EQ(content, R"({"key":"value"})");

    std::remove(path.c_str());
}

TEST_F(JsonTest, WriteJsonToFileFails)
{
    Poco::JSON::Object::Ptr object = new Poco::JSON::Object();
    object->set("key", "value");

    std::string path = "/non/existent/path/test.json";

    EXPECT_EQ(WriteJsonToFile(object, path), aos::ErrorEnum::eFailed);
}

TEST_F(JsonTest, Stringify)
{
    Poco::JSON::Object::Ptr object = new Poco::JSON::Object();
    object->set("key", "value");

    std::string stringified = Stringify(object);

    aos::Error         err;
    Poco::Dynamic::Var result;

    ASSERT_NO_THROW(aos::Tie(result, err) = ParseJson(stringified));
    ASSERT_EQ(result.type(), typeid(Poco::JSON::Object::Ptr));

    ASSERT_TRUE(object->has("key"));
    EXPECT_EQ(object->get("key").convert<std::string>(), "value");
}

} // namespace aos::common::utils
