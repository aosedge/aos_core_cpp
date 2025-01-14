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

namespace aos::common::utils {

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

    bool operator==(const Time& time) const
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

TEST_F(TimeTest, ParseDurationSucceeds)
{
    using namespace std::chrono_literals;

    struct Test {
        std::string              input;
        std::chrono::nanoseconds expected;
    };

    std::vector<Test> tests = {
        // duration string
        {"1ns", 1ns},
        {"1us", 1us},
        {"1µs", 1us},
        {"1ms", 1ms},
        {"1s", 1s},
        {"1m", 1min},
        {"1h", 1h},
        {"1d", 24h},
        {"1w", 24h * 7},
        {"1y", 24h * 365},
        {"200s", 200s},
        {"1h20m1s", 1h + 20min + 1s},
        {"15h20m20s20ms", 15h + 20min + 20s + 20ms},
        {"20h20m20s200ms100us", 20h + 20min + 20s + 200ms + 100us},
        {"20h20m20s200ms100us100ns", 20h + 20min + 20s + 200ms + 100us + 100ns},
        {"1y1w1d1h1m1s1ms1us", 24h * 365 + 24h * 7 + 24h + 1h + 1min + 1s + 1ms + 1us},
        // ISO 8601 duration string
        {"P1Y", 24h * 365},
        {"P1Y1D", 24h * 365 + 24h},
        {"PT1S", 1s},
        {"PT1M1S", 1min + 1s},
        {"PT1H1M1S", 1h + 1min + 1s},
        {"P1Y1M1W1DT1H1M1S", 24h * 365 + (24h * 365 / 12) + (24h * 7) + 24h + 1h + 1min + 1s},
        // floating number string
        {"10", 10s},
        {"10.1", 10s},
        {"10.5", 11s},
        {"10.9", 11s},
    };

    for (const auto& test : tests) {
        auto [duration, error] = ParseDuration(test.input);
        ASSERT_TRUE(error.IsNone());
        ASSERT_EQ(duration, test.expected);
    }
}

TEST_F(TimeTest, ParseDurationFromInvalidString)
{
    std::vector<std::string> tests = {"1#", "1a", "1s1", "sss", "s111", "%12d", "y1y", "/12d"};

    for (const auto& test : tests) {
        auto [duration, error] = ParseDuration(test);
        ASSERT_FALSE(error.IsNone());
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
    test::InitLog();

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
