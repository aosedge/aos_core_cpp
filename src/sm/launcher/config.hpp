/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_CONFIG_HPP_
#define AOS_SM_LAUNCHER_CONFIG_HPP_

#include <vector>

#include "runtimes/config.hpp"

namespace aos::sm::launcher {

/**
 * Launcher configuration.
 */
struct Config {
    std::vector<RuntimeConfig> mRuntimes;
};

} // namespace aos::sm::launcher

#endif
