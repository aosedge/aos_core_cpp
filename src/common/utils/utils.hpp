/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_UTILS_UTILS_HPP_
#define AOS_COMMON_UTILS_UTILS_HPP_

#include <string>
#include <vector>

#include <core/common/tools/error.hpp>

namespace aos::common::utils {

RetWithError<std::string> ExecCommand(const std::vector<std::string>& args);

} // namespace aos::common::utils

#endif
