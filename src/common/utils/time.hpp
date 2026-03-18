/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_UTILS_TIME_HPP_
#define AOS_COMMON_UTILS_TIME_HPP_

#include <chrono>
#include <optional>
#include <string>

#include <core/common/tools/error.hpp>
#include <core/common/tools/time.hpp>

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

} // namespace aos::common::utils

#endif
