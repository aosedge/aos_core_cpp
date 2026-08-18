/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <gmock/gmock.h>

#include <core/common/tests/utils/log.hpp>

#include <common/utils/parser.hpp>

using namespace testing;

/***********************************************************************************************************************
 * Consts
 **********************************************************************************************************************/

static constexpr auto cExpectedKey   = "key";
static constexpr auto cExpectedValue = "value";
static constexpr auto cEmptyString   = "";
static constexpr auto cDelimiter1    = ":";
static constexpr auto cDelimiter2    = "=";
static constexpr auto cTrim          = true;
static constexpr auto cDoNoTrim      = false;
static constexpr auto cSpaces        = "    ";

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class ParserTest : public Test { };

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(ParserTest, ParseEmptyStringReturnsNullopt)
{
    const auto result = aos::common::utils::ParseKeyValue(cEmptyString);
    EXPECT_FALSE(result.has_value());
}

TEST_F(ParserTest, ParseSucceeds)
{
    const auto line = std::string(cExpectedKey).append(cDelimiter1).append(cExpectedValue);

    const auto result = aos::common::utils::ParseKeyValue(line, cTrim, cDelimiter1);
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->mKey, cExpectedKey);
    EXPECT_EQ(result->mValue, cExpectedValue);
}

TEST_F(ParserTest, ParseFailsOnInvalidDelimiter)
{
    const auto line = std::string(cExpectedKey).append(cDelimiter1).append(cExpectedValue);

    const auto result = aos::common::utils::ParseKeyValue(line, cTrim, cDelimiter2);
    ASSERT_FALSE(result.has_value());
}

TEST_F(ParserTest, ParseFailsOnNoValue)
{
    const auto line = std::string(cExpectedKey).append(cDelimiter1);

    const auto result = aos::common::utils::ParseKeyValue(line, cTrim, cDelimiter2);
    ASSERT_FALSE(result.has_value());
}

TEST_F(ParserTest, ParseResultIsTrimmed)
{
    const auto line = std::string(cExpectedKey)
                          .append(cSpaces)
                          .append(cDelimiter1)
                          .append(cSpaces)
                          .append(cExpectedValue)
                          .append(cSpaces);

    const auto result = aos::common::utils::ParseKeyValue(line, cTrim, cDelimiter1);
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->mKey, cExpectedKey);
    EXPECT_EQ(result->mValue, cExpectedValue);
}

TEST_F(ParserTest, ParseResultIsNotTrimmedIfTrimDisabled)
{
    const auto key   = std::string(cExpectedKey).append(cSpaces);
    const auto value = std::string(cSpaces).append(cExpectedValue).append(cSpaces);
    const auto line  = std::string(key).append(cDelimiter1).append(value);

    const auto result = aos::common::utils::ParseKeyValue(line, cDoNoTrim, cDelimiter1);
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->mKey, key);
    EXPECT_EQ(result->mValue, value);
}

TEST_F(ParserTest, ParsePortRangeAcceptsSinglePort)
{
    for (const auto& [value, expected] :
        std::vector<std::pair<std::string, uint16_t>> {{"1", 1}, {"80", 80}, {"1515", 1515}, {"65535", 65535}}) {
        const auto result = aos::common::utils::ParsePortRange(value);

        ASSERT_TRUE(result.has_value()) << value;
        EXPECT_EQ(*result, (aos::common::utils::PortRange {expected, expected})) << value;
    }
}

TEST_F(ParserTest, ParsePortRangeAcceptsRange)
{
    for (const auto& [value, first, last] : std::vector<std::tuple<std::string, uint16_t, uint16_t>> {
             {"8089:8090", 8089, 8090}, {"7400:7650", 7400, 7650}, {"1:65535", 1, 65535}, {"80:80", 80, 80}}) {
        const auto result = aos::common::utils::ParsePortRange(value);

        ASSERT_TRUE(result.has_value()) << value;
        EXPECT_EQ(*result, (aos::common::utils::PortRange {first, last})) << value;
    }
}

TEST_F(ParserTest, ParsePortRangeRejectsInvalidValues)
{
    for (const auto& value : std::vector<std::string> {"", "0", "0:10", "10:5", "1:70000", "70000", "abc",
             "7400:", ":7650", ":", "7400::7650", "7400:7500:7600", " 80", "80 ", "+80", "80/tcp", "8o", "8080-8081"}) {
        EXPECT_FALSE(aos::common::utils::ParsePortRange(value).has_value()) << value;
    }
}
