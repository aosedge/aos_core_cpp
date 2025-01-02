/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIME_HPP_
#define TIME_HPP_

#include <chrono>
#include <optional>
#include <string>

#include <aos/common/tools/error.hpp>
#include <aos/common/tools/time.hpp>

namespace aos::common::utils {

/***********************************************************************************************************************
 * Types
 **********************************************************************************************************************/

/**
 * Duration type.
 */
using Duration = std::chrono::duration<int64_t, std::nano>;

/***********************************************************************************************************************
 * Functions
 **********************************************************************************************************************/

/**
 * Parses duration from string.
 *
 * @param duration duration string.
 * @return parsed duration.
 */
aos::RetWithError<Duration> ParseDuration(const std::string& duration);

/**
 * Parses ISO8601 duration from string.
 *
 * @param duration duration string.
 * @return parsed duration.
 */
aos::RetWithError<Duration> ParseISO8601Duration(const std::string& duration);

/**
 * Formats ISO8601 duration string.
 *
 * @param duration duration.
 * @return aos::RetWithError<std::string>.
 */
aos::RetWithError<std::string> FormatISO8601Duration(const Duration& duration);

/**
 * Creates time object from a UTC formatted string.
 *
 * @param utcTimeStr UTC formatted time string.
 * @return aos::RetWithError<aos::Time>.
 */
aos::RetWithError<aos::Time> FromUTCString(const std::string& utcTimeStr);

/**
 * Converts time into a UTC string.
 *
 * @param time time object.
 * @return aos::RetWithError<std::string>.
 */
aos::RetWithError<std::string> ToUTCString(const aos::Time& time);

} // namespace aos::common::utils

#endif
