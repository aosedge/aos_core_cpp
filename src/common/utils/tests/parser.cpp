/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gmock/gmock.h>

#include <aos/test/log.hpp>

#include "utils/parser.hpp"

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
