/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024s EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <aos/test/log.hpp>

#include "utils/exception.hpp"
#include "utils/time.hpp"

using namespace testing;

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

namespace {

void SetTimezone(const std::string& timezone)
{
    setenv("TZ", timezone.c_str(), 1);
    tzset();
}

class ExpectedDateTime {
public:
    ExpectedDateTime& SetYear(int year)
    {
        mYear = year;
        return *this;
    }

    ExpectedDateTime& SetMonth(int month)
    {
        mMonth = month;
        return *this;
    }

    ExpectedDateTime& SetDay(int day)
    {
        mDay = day;
        return *this;
    }

    ExpectedDateTime& SetHour(int hour)
    {
        mHour = hour;
        return *this;
    }

    ExpectedDateTime& SetMinute(int minute)
    {
        mMinute = minute;
        return *this;
    }

    ExpectedDateTime& SetSecond(int second)
    {
        mSecond = second;
        return *this;
    }

    bool operator==(const aos::Time& time) const
    {
        int day, month, year, hour, minute, second;

        auto err = time.GetDate(&day, &month, &year);
        AOS_ERROR_CHECK_AND_THROW("failed to get date", err);

        if (mYear.has_value() && mYear.value() != year) {
            return false;
        }

        if (mMonth.has_value() && mMonth.value() != month) {
            return false;
        }

        if (mDay.has_value() && mDay.value() != day) {
            return false;
        }

        err = time.GetTime(&hour, &minute, &second);
        AOS_ERROR_CHECK_AND_THROW("failed to get time", err);

        if (mHour.has_value() && mHour.value() != hour) {
            return false;
        }

        if (mMinute.has_value() && mMinute.value() != minute) {
            return false;
        }

        if (mSecond.has_value() && mSecond.value() != second) {
            return false;
        }

        return true;
    }

private:
    std::optional<int> mYear;
    std::optional<int> mMonth;
    std::optional<int> mDay;
    std::optional<int> mHour;
    std::optional<int> mMinute;
    std::optional<int> mSecond;
};

} // namespace

class TimeTest : public Test {
protected:
    void SetUp() override { test::InitLog(); }
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(TimeTest, ParseDurationFromValidString)
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
        auto [duration, error] = ParseDuration(test.input);
        ASSERT_TRUE(error.IsNone());
        ASSERT_EQ(duration, test.expected);
    }
}

TEST_F(TimeTest, ParseDurationFromInvalidString)
{
    std::vector<std::string> tests = {"1", "1a", "1s1", "sss", "s111", "%12d", "y1y", "/12d"};

    for (const auto& test : tests) {
        auto [duration, error] = ParseDuration(test);
        ASSERT_FALSE(error.IsNone());
    }
}

TEST_F(TimeTest, ParseISO8601Duration)
{
    using namespace std::chrono_literals;

    struct Test {
        std::string              input;
        std::chrono::nanoseconds expected;
    };

    std::vector<Test> tests = {
        {"P1Y", 24h * 365},
        {"P1Y1D", 24h * 365 + 24h},
        {"PT1S", 1s},
        {"PT1M1S", 1min + 1s},
        {"PT1H1M1S", 1h + 1min + 1s},
        {"P1Y1M1W1DT1H1M1S", 24h * 365 + (24h * 365 / 12) + (24h * 7) + 24h + 1h + 1min + 1s},
    };

    for (const auto& test : tests) {
        auto [duration, err] = ParseISO8601Duration(test.input);

        ASSERT_TRUE(err.IsNone()) << "input: " << test.input << ", err: " << err.Message();
        ASSERT_EQ(duration, test.expected) << "input: " << test.input << ", expected: " << test.expected.count() << ", "
                                           << "actual: " << duration.count();
    }
}

TEST_F(TimeTest, FormatISO8601Duration)
{
    using namespace std::chrono_literals;

    struct Test {
        std::string              expected;
        std::chrono::nanoseconds Duration;
    };

    std::vector<Test> tests = {
        {"PT1S", 1s},
        {"PT1M", 1min},
        {"PT1H", 1h},
        {"PT1H1M1S", 1h + 1min + 1s},
        {"P1D", 24h},
        {"P1W", 24h * 7},
        {"P1M", 24h * 365 / 12},
        {"P1Y", 24h * 365},
        {"P1Y1M1W1DT1H1M1S", 24h * 365 + (24h * 365 / 12) + (24h * 7) + 24h + 1h + 1min + 1s},
    };

    for (const auto& test : tests) {
        auto [durationStr, error] = FormatISO8601Duration(test.Duration);

        ASSERT_TRUE(error.IsNone()) << "expected: " << test.expected << ", err: " << error.Message();
        ASSERT_EQ(durationStr, test.expected);
    }
}

TEST_F(TimeTest, FromToUTCString)
{
    struct Test {
        std::string      input;
        std::string      expectedUTCString;
        std::string      timezone;
        ExpectedDateTime expectedLocalTime;
    };

    std::vector<Test> tests = {
        {"2024-01-01T00:00:00Z", "2024-01-01T00:00:00Z", "GMT+1",
            ExpectedDateTime().SetYear(2024).SetDay(1).SetMonth(1).SetHour(1)},
        {"2024-01-01T00:00:00Z", "2024-01-01T00:00:00Z", "GMT-1",
            ExpectedDateTime().SetYear(2023).SetDay(31).SetMonth(12).SetHour(23).SetMinute(0)},
    };

    for (const auto& test : tests) {
        SetTimezone(test.timezone);

        auto [time, fromError] = FromUTCString(test.input);
        ASSERT_TRUE(fromError.IsNone());

        auto [timeStr, toError] = ToUTCString(time);

        ASSERT_TRUE(toError.IsNone());
        ASSERT_EQ(timeStr, test.expectedUTCString);

        ASSERT_EQ(test.expectedLocalTime, time);
    }
}

} // namespace aos::common::utils
