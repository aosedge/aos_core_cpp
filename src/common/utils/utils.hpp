/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef UTILS_HPP_
#define UTILS_HPP_

#include <string>
#include <vector>

#include <aos/common/tools/error.hpp>

namespace aos::common::utils {

RetWithError<std::string> ExecCommand(const std::vector<std::string>& args);

} // namespace aos::common::utils

#endif // UTILS_HPP_
