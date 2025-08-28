/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>

#include <common/utils/exception.hpp>
#include <common/utils/time.hpp>

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
        int day = 0, month = 0, year = 0, hour = 0, minute = 0, second = 0;

        auto err = time.GetDate(&day, &month, &year);
        AOS_ERROR_CHECK_AND_THROW(err, "failed to get date");

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
        AOS_ERROR_CHECK_AND_THROW(err, "failed to get time");

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
    void SetUp() override { tests::utils::InitLog(); }
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(TimeTest, ParseDurationSucceeds)
{
    struct Test {
        std::string mInput;
        Duration    mExpected;
    };

    std::vector<Test> tests = {
        // duration string
        {"1ns", Time::cNanoseconds},
        {"1us", Time::cMicroseconds},
        {"1µs", Time::cMicroseconds},
        {"1ms", Time::cMilliseconds},
        {"1s", Time::cSeconds},
        {"1m", Time::cMinutes},
        {"1h", Time::cHours},
        {"1d", Time::cDay},
        {"1w", Time::cWeek},
        {"1y", Time::cYear},
        {"200s", Time::cSeconds * 200},
        {"1h20m1s", Time::cHours + Time::cMinutes * 20 + Time::cSeconds},
        {"15h20m20s20ms", Time::cHours * 15 + Time::cMinutes * 20 + Time::cSeconds * 20 + Time::cMilliseconds * 20},
        {"20h20m20s200ms100us",
            Time::cHours * 20 + Time::cMinutes * 20 + Time::cSeconds * 20 + Time::cMilliseconds * 200
                + Time::cMicroseconds * 100},
        {"20h20m20s200ms100us100ns",
            Time::cHours * 20 + Time::cMinutes * 20 + Time::cSeconds * 20 + Time::cMilliseconds * 200
                + Time::cMicroseconds * 100 + Time::cNanoseconds * 100},
        {"1y1w1d1h1m1s1ms1us",
            Time::cYear + Time::cWeek + Time::cDay + Time::cHours + Time::cMinutes + Time::cSeconds
                + Time::cMilliseconds + Time::cMicroseconds},
        // ISO 8601 duration string
        {"P1Y", Time::cYear},
        {"-P1Y", -Time::cYear},
        {"P1Y1D", Time::cYear + Time::cDay},
        {"PT1S", Time::cSeconds},
        {"PT1M1S", Time::cMinutes + Time::cSeconds},
        {"PT1H1M1S", Time::cHours + Time::cMinutes + Time::cSeconds},
        {"P1Y1M1W1DT1H1M1S",
            Time::cYear + Time::cMonth + Time::cWeek + Time::cDay + Time::cHours + Time::cMinutes + Time::cSeconds},
        // floating number string
        {"10", Time::cSeconds * 10},
        {"10.1", Time::cSeconds * 10},
        {"10.5", Time::cSeconds * 11},
        {"10.9", Time::cSeconds * 11},
    };

    for (const auto& test : tests) {
        auto [duration, error] = ParseDuration(test.mInput);
        ASSERT_TRUE(error.IsNone()) << "input= " << test.mInput << ", err=" << error.Message();
        ASSERT_EQ(duration.Nanoseconds(), test.mExpected.Nanoseconds()) << "input= " << test.mInput;
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

TEST_F(TimeTest, FromToUTCString)
{
    tests::utils::InitLog();

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
