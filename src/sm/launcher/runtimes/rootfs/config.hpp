/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_ROOTFS_CONFIG_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_ROOTFS_CONFIG_HPP_

#include <optional>
#include <string>
#include <vector>

#include <core/common/tools/time.hpp>
#include <core/common/types/common.hpp>

#include <common/utils/json.hpp>
#include <sm/config/config.hpp>

namespace aos::sm::launcher {

/**
 * Rootfs runtime config.
 */
struct RootfsConfig {
    std::string              mWorkingDir;
    std::string              mVersionFilePath;
    std::vector<std::string> mHealthCheckServices;
};

/**
 * Parses rootfs runtime config.
 *
 * @param config runtime config.
 * @param[out] rootfsConfig rootfs runtime config.
 */
Error ParseConfig(const RuntimeConfig& config, RootfsConfig& rootfsConfig);

} // namespace aos::sm::launcher

#endif
