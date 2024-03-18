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

namespace UtilsTime {

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

} // namespace UtilsTime

#endif
