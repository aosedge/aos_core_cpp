/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_UTILS_EXEC_HPP_
#define AOS_COMMON_UTILS_EXEC_HPP_

#include <initializer_list>
#include <string>
#include <vector>

#include <core/common/tools/error.hpp>
#include <core/common/types/common.hpp>

namespace aos::common::utils {

/**
 * Executes command and return its output.
 *
 * @param args command arguments (first argument is program name).
 * @param expectedExitCodes expected command exit codes (default is 0).
 * @return RetWithError<std::string>.
 */
RetWithError<std::string> ExecCommand(
    const std::vector<std::string>& args, const std::initializer_list<int>& expectedExitCodes = {0});

/**
 * Executes a command that daemonizes into a long-running process, letting it inherit this process's
 * stdout/stderr instead of a pipe that would have to be closed once the immediate command exits, cutting
 * off the daemonized process's own output.
 *
 * @param args command arguments (first argument is program name).
 * @param expectedExitCodes expected command exit codes (default is 0).
 * @return Error.
 */
Error ExecDetachedCommand(
    const std::vector<std::string>& args, const std::initializer_list<int>& expectedExitCodes = {0});

} // namespace aos::common::utils

#endif
