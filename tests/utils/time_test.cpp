/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024s EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include "utils/time.hpp"

using namespace testing;

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST(TimeTest, ParseDurationFromValidString)
{
    struct Test {
        std::string              input;
        std::chrono::nanoseconds expected;
    };

    std::vector<Test> tests = {
        {"1ns", std::chrono::nanoseconds(1)},
        {"1us", std::chrono::microseconds(1)},
        {"1µs", std::chrono::microseconds(1)},
        {"1ms", std::chrono::milliseconds(1)},
        {"1s", std::chrono::seconds(1)},
        {"1m", std::chrono::minutes(1)},
        {"1h", std::chrono::hours(1)},
        {"1d", std::chrono::hours(24)},
        {"1w", std::chrono::hours(24 * 7)},
        {"1y", std::chrono::hours(24 * 365)},
        {"200s", std::chrono::seconds(200)},
        {"1h20m1s", std::chrono::hours(1) + std::chrono::minutes(20) + std::chrono::seconds(1)},
        {"15h20m20s20ms",
            std::chrono::hours(15) + std::chrono::minutes(20) + std::chrono::seconds(20)
                + std::chrono::milliseconds(20)},
        {"20h20m20s200ms100us",
            std::chrono::hours(20) + std::chrono::minutes(20) + std::chrono::seconds(20)
                + std::chrono::milliseconds(200) + std::chrono::microseconds(100)},
        {"20h20m20s200ms100us100ns",
            std::chrono::hours(20) + std::chrono::minutes(20) + std::chrono::seconds(20)
                + std::chrono::milliseconds(200) + std::chrono::microseconds(100) + std::chrono::nanoseconds(100)},
        {"1y1w1d1h1m1s1ms1us",
            std::chrono::hours(24 * 365 + 24 * 7 + 24) + std::chrono::hours(1) + std::chrono::minutes(1)
                + std::chrono::seconds(1) + std::chrono::milliseconds(1) + std::chrono::microseconds(1)},

    };

    for (const auto& test : tests) {
        auto [duration, error] = UtilsTime::ParseDuration(test.input);
        ASSERT_TRUE(error.IsNone());
        ASSERT_EQ(duration, test.expected);
    }
}

TEST(TimeTest, ParseDurationFromInvalidString)
{
    std::vector<std::string> tests = {"1", "1a", "1s1", "sss", "s111", "%12d", "y1y", "/12d"};

    for (const auto& test : tests) {
        auto [duration, error] = UtilsTime::ParseDuration(test);
        ASSERT_FALSE(error.IsNone());
    }
}
