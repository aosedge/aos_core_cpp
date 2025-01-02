/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <array>
#include <map>
#include <regex>
#include <sstream>

#include <aos/common/tools/time.hpp>

#include "utils/time.hpp"

namespace aos::common::utils {

namespace {

/***********************************************************************************************************************
 * Constants
 **********************************************************************************************************************/

constexpr auto cSecondDuration = Duration(std::chrono::seconds(1));
constexpr auto cMinuteDuration = Duration(std::chrono::minutes(1));
constexpr auto cHourDuration   = Duration(std::chrono::hours(1));
constexpr auto cDayDuration    = cHourDuration * 24;
constexpr auto cWeekDuration   = 7 * cDayDuration;
constexpr auto cYearDuration   = 365 * cDayDuration;
constexpr auto cMonthDuration  = cYearDuration / 12;

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

aos::RetWithError<Duration> ParseISO8601DurationPeriod(const std::string& period)
{
    Duration totalDuration {};

    if (period.empty()) {
        return {totalDuration};
    }

    std::smatch match;
    std::regex  iso8601DurationPattern(R"(P(?:(\d+)Y)?(?:(\d+)M)?(?:(\d+)W)?(?:(\d+)D)?)");

    if (!std::regex_match(period, match, iso8601DurationPattern) || match.size() != 5) {
        return {{}, Error(aos::ErrorEnum::eInvalidArgument, "invalid ISO8601 duration format")};
    }

    if (match[1].matched) {
        totalDuration += std::stoi(match[1].str()) * cYearDuration;
    }

    if (match[2].matched) {
        totalDuration += std::stoi(match[2].str()) * cMonthDuration;
    }

    if (match[3].matched) {
        totalDuration += std::stoi(match[3].str()) * cWeekDuration;
    }

    if (match[4].matched) {
        totalDuration += std::stoi(match[4].str()) * cDayDuration;
    }

    return totalDuration;
}

aos::RetWithError<Duration> ParseISO8601DurationTime(const std::string& time)
{
    std::chrono::nanoseconds totalDuration {};

    if (time.empty()) {
        return {totalDuration};
    }

    std::smatch match;
    std::regex  iso8601DurationPattern(R"(T(?:(\d+)H)?(?:(\d+)M)?(?:(\d+)S)?)");

    if (!std::regex_match(time, match, iso8601DurationPattern) || match.size() != 4) {
        return {{}, Error(aos::ErrorEnum::eInvalidArgument, "invalid ISO8601 duration format")};
    }

    if (match[1].matched) {
        totalDuration += std::stoi(match[1].str()) * cHourDuration;
    }

    if (match[2].matched) {
        totalDuration += std::stoi(match[2].str()) * cMinuteDuration;
    }

    if (match[3].matched) {
        totalDuration += std::stoi(match[3].str()) * cSecondDuration;
    }

    return totalDuration;
}

std::string FormatISO8601DurationPeriod(int64_t& total)
{
    std::ostringstream oss;

    oss << "P";

    if (auto years = total / cYearDuration.count(); years > 0) {
        oss << years << "Y";

        total %= cYearDuration.count();
    }

    if (auto months = total / cMonthDuration.count(); months > 0) {
        oss << months << "M";

        total %= cMonthDuration.count();
    }

    if (auto weeks = total / cWeekDuration.count(); weeks > 0) {
        oss << weeks << "W";

        total %= cWeekDuration.count();
    }

    if (auto days = total / cDayDuration.count(); days > 0) {
        oss << days << "D";

        total %= cDayDuration.count();
    }

    return oss.str();
}

std::string FormatISO8601DurationTime(int64_t& total)
{
    std::ostringstream oss;

    auto hours = total / cHourDuration.count();
    total %= cHourDuration.count();

    auto minutes = total / cMinuteDuration.count();
    total %= cMinuteDuration.count();

    auto seconds = total / cSecondDuration.count();
    total %= cSecondDuration.count();

    if (hours || minutes || seconds) {
        oss << "T";

        if (hours) {
            oss << hours << "H";
        }

        if (minutes) {
            oss << minutes << "M";
        }

        if (seconds) {
            oss << seconds << "S";
        }
    }

    return oss.str();
}

}; // namespace

aos::RetWithError<Duration> ParseDuration(const std::string& durationStr)
{
    static const std::map<std::string, std::chrono::nanoseconds> units
        = {{"ns", std::chrono::nanoseconds(1)}, {"us", std::chrono::microseconds(1)},
            {"µs", std::chrono::microseconds(1)}, {"ms", std::chrono::milliseconds(1)}, {"s", std::chrono::seconds(1)},
            {"m", std::chrono::minutes(1)}, {"h", std::chrono::hours(1)}, {"d", std::chrono::hours(24)},
            {"w", std::chrono::hours(24 * 7)}, {"y", std::chrono::hours(24 * 365)}};

    std::chrono::nanoseconds totalDuration {};
    std::regex               wholeStringPattern(R"((\d+(ns|us|µs|ms|s|m|h|d|w|y))+$)");

    if (!std::regex_match(durationStr, wholeStringPattern)) {
        return {totalDuration, aos::ErrorEnum::eInvalidArgument};
    }

    std::regex           componentPattern(R"((\d+)(ns|us|µs|ms|s|m|h|d|w|y))");
    auto                 begin = std::sregex_iterator(durationStr.begin(), durationStr.end(), componentPattern);
    std::sregex_iterator end;

    for (auto i = begin; i != end; ++i) {
        std::smatch match = *i;
        std::string unit  = match[2].str();

        std::transform(unit.begin(), unit.end(), unit.begin(), ::tolower);

        totalDuration += units.at(unit) * std::stoll(match[1].str());
    }

    return totalDuration;
}

aos::RetWithError<Duration> ParseISO8601Duration(const std::string& duration)
{
    Duration    totalDuration {};
    std::smatch match;
    std::regex  iso8601DurationPattern(R"(^(P(?:\d+Y)?(?:\d+M)?(?:\d+W)?(?:\d+D)?)?(T(?:\d+H)?(?:\d+M)?(?:\d+S)?)?$)");

    if (!std::regex_match(duration, match, iso8601DurationPattern) || match.size() != 3) {
        return {{}, Error(aos::ErrorEnum::eInvalidArgument, "invalid ISO8601 duration format")};
    }

    auto [delta, err] = ParseISO8601DurationPeriod(match[1].str());
    if (!err.IsNone()) {
        return {{}, AOS_ERROR_WRAP(err)};
    }

    totalDuration += delta;

    Tie(delta, err) = ParseISO8601DurationTime(match[2].str());
    if (!err.IsNone()) {
        return {{}, AOS_ERROR_WRAP(err)};
    }

    totalDuration += delta;

    return totalDuration;
}

aos::RetWithError<std::string> FormatISO8601Duration(const Duration& duration)
{
    auto total = duration.count();

    auto durationStr = FormatISO8601DurationPeriod(total);
    if (durationStr.empty()) {
        return {"", Error(ErrorEnum::eFailed, "failed to format ISO8601 duration")};
    }

    return durationStr.append(FormatISO8601DurationTime(total));
}

aos::RetWithError<aos::Time> FromUTCString(const std::string& utcTimeStr)
{
    struct tm timeInfo = {};

    if (!strptime(utcTimeStr.c_str(), "%Y-%m-%dT%H:%M:%SZ", &timeInfo)) {
        return {Time(), ErrorEnum::eInvalidArgument};
    }

    return {aos::Time::Unix(mktime(&timeInfo)), ErrorEnum::eNone};
}

aos::RetWithError<std::string> ToUTCString(const aos::Time& time)
{
    tm   timeInfo {};
    auto timespec = time.UnixTime();

    timespec.tv_sec = timegm(localtime_r(&timespec.tv_sec, &timeInfo));

    if (!gmtime_r(&timespec.tv_sec, &timeInfo)) {
        return {"", ErrorEnum::eFailed};
    }

    std::array<char, aos::cTimeStrLen> buffer;

    auto size = strftime(buffer.begin(), buffer.size(), "%Y-%m-%dT%H:%M:%SZ", &timeInfo);

    if (size == 0) {
        return {"", ErrorEnum::eFailed};
    }

    return {{buffer.begin(), buffer.begin() + size}, ErrorEnum::eNone};
}

} // namespace aos::common::utils
