/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <map>
#include <regex>
#include <sstream>

#include "time.hpp"

namespace UtilsTime {

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

} // namespace UtilsTime
