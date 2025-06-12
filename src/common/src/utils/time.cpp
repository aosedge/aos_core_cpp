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
 * Static
 **********************************************************************************************************************/

RetWithError<Duration> ParseStringDuration(const std::string& durationStr)
{
    static const std::map<std::string, Duration> units = {{"ns", Time::cNanoseconds}, {"us", Time::cMicroseconds},
        {"µs", Time::cMicroseconds}, {"ms", Time::cMilliseconds}, {"s", Time::cSeconds}, {"m", Time::cMinutes},
        {"h", Time::cHours}, {"d", Time::cDay}, {"w", Time::cWeek}, {"y", Time::cYear}};

    Duration totalDuration {};

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

RetWithError<Duration> ParseISO8601DurationPeriod(const std::string& period)
{
    Duration totalDuration {};

    if (period.empty()) {
        return {totalDuration};
    }

    std::smatch match;
    std::regex  iso8601DurationPattern(R"(-?P(?:(\d+)Y)?(?:(\d+)M)?(?:(\d+)W)?(?:(\d+)D)?)");

    if (!std::regex_match(period, match, iso8601DurationPattern) || match.size() != 5) {
        return {{}, Error(ErrorEnum::eInvalidArgument, "invalid ISO8601 duration format")};
    }

    if (match[1].matched) {
        totalDuration += Time::cYear * std::stoi(match[1].str());
    }

    if (match[2].matched) {
        totalDuration += Time::cMonth * std::stoi(match[2].str());
    }

    if (match[3].matched) {
        totalDuration += Time::cWeek * std::stoi(match[3].str());
    }

    if (match[4].matched) {
        totalDuration += Time::cDay * std::stoi(match[4].str());
    }

    return totalDuration;
}

RetWithError<Duration> ParseISO8601DurationTime(const std::string& time)
{
    Duration totalDuration {};

    if (time.empty()) {
        return totalDuration;
    }

    std::smatch match;
    std::regex  iso8601DurationPattern(R"(T(?:(\d+)H)?(?:(\d+)M)?(?:(\d+)S)?)");

    if (!std::regex_match(time, match, iso8601DurationPattern) || match.size() != 4) {
        return {{}, Error(ErrorEnum::eInvalidArgument, "invalid ISO8601 duration format")};
    }

    if (match[1].matched) {
        totalDuration += Time::cHours * std::stoi(match[1].str());
    }

    if (match[2].matched) {
        totalDuration += Time::cMinutes * std::stoi(match[2].str());
    }

    if (match[3].matched) {
        totalDuration += Time::cSeconds * std::stoi(match[3].str());
    }

    return totalDuration;
}

RetWithError<Duration> ParseISO8601Duration(const std::string& duration)
{
    Duration    totalDuration {};
    std::smatch match;
    std::regex iso8601DurationPattern(R"(^(-?P(?:\d+Y)?(?:\d+M)?(?:\d+W)?(?:\d+D)?)?(T(?:\d+H)?(?:\d+M)?(?:\d+S)?)?$)");

    if (!std::regex_match(duration, match, iso8601DurationPattern) || match.size() != 3) {
        return {{}, Error(ErrorEnum::eInvalidArgument, "invalid ISO8601 duration format")};
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

    if (duration[0] == '-') {
        totalDuration = -totalDuration;
    }

    return totalDuration;
}

}; // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

RetWithError<Duration> ParseDuration(const std::string& durationStr)
{
    if (durationStr.empty()) {
        return {{}, AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "empty duration string"))};
    }

    if (durationStr[0] == 'P' || durationStr.find("-P") != std::string::npos) {
        return ParseISO8601Duration(durationStr);
    }

    const std::regex floatPattern(R"(^-?\d*(\.\d+)?$)");

    if (std::regex_match(durationStr, floatPattern)) {
        return Time::cSeconds * static_cast<int>(std::stod(durationStr) + 0.5);
    }

    const std::regex durationStringPattern(R"((\d+(ns|us|µs|ms|s|m|h|d|w|y))+$)");

    if (std::regex_match(durationStr, durationStringPattern)) {
        return ParseStringDuration(durationStr);
    }

    return {{}, Error(ErrorEnum::eInvalidArgument, "invalid duration string")};
}

RetWithError<Time> FromUTCString(const std::string& utcTimeStr)
{
    struct tm timeInfo = {};

    if (!strptime(utcTimeStr.c_str(), "%Y-%m-%dT%H:%M:%SZ", &timeInfo)) {
        return {Time(), ErrorEnum::eInvalidArgument};
    }

    return {Time::Unix(mktime(&timeInfo)), ErrorEnum::eNone};
}

RetWithError<std::string> ToUTCString(const Time& time)
{
    tm   timeInfo {};
    auto timespec = time.UnixTime();

    timespec.tv_sec = timegm(localtime_r(&timespec.tv_sec, &timeInfo));

    if (!gmtime_r(&timespec.tv_sec, &timeInfo)) {
        return {"", ErrorEnum::eFailed};
    }

    std::array<char, cTimeStrLen> buffer;

    auto size = strftime(buffer.begin(), buffer.size(), "%Y-%m-%dT%H:%M:%SZ", &timeInfo);

    if (size == 0) {
        return {"", ErrorEnum::eFailed};
    }

    return {{buffer.begin(), buffer.begin() + size}, ErrorEnum::eNone};
}

} // namespace aos::common::utils
