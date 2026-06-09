/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_FILECOPY_CONFIG_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_FILECOPY_CONFIG_HPP_

#include <string>

#include <sm/config/config.hpp>

#include <sm/launcher/runtimes/config.hpp>

namespace aos::sm::launcher {

/**
 * File copy runtime config.
 */
struct FileCopyConfig {
    std::string mTargetPath;
    std::string mRuntimeDir;
};

/**
 * Parses file copy runtime config.
 *
 * @param config runtime config.
 * @param[out] fileCopyConfig file copy runtime config.
 * @return Error.
 */
Error ParseConfig(const RuntimeConfig& config, FileCopyConfig& fileCopyConfig);

} // namespace aos::sm::launcher

#endif
