/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_CONFIG_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_CONFIG_HPP_

#include <optional>
#include <string>
#include <vector>

#include <core/common/tools/time.hpp>
#include <core/common/types/common.hpp>

namespace aos::sm::launcher {

/**
 * Rootfs runtime config.
 */
struct RootfsConfig : public RuntimeInfo {
    std::string              mCurrentInstanceFile;
    std::string              mCurrentVersionFile;
    std::string              mUpdateInstanceFile;
    std::string              mUpdateDir;
    std::vector<std::string> mUpdateServicesToCheck;
};

/**
 * Launcher config.
 */
struct Config {
    std::optional<RootfsConfig> mRootfsConfig;
};

} // namespace aos::sm::launcher

#endif
