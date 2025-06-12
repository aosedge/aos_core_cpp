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
 * Functions
 **********************************************************************************************************************/

/**
 * Parses duration from string.
 *
 * @param duration duration string.
 * @return parsed duration.
 */
RetWithError<Duration> ParseDuration(const std::string& duration);

/**
 * Creates time object from a UTC formatted string.
 *
 * @param utcTimeStr UTC formatted time string.
 * @return RetWithError<Time>.
 */
RetWithError<Time> FromUTCString(const std::string& utcTimeStr);

/**
 * Converts time into a UTC string.
 *
 * @param time time object.
 * @return RetWithError<std::string>.
 */
RetWithError<std::string> ToUTCString(const Time& time);

} // namespace aos::common::utils

#endif
