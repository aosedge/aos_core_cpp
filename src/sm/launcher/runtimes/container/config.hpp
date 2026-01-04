/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_CONTAINER_CONFIG_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_CONTAINER_CONFIG_HPP_

#include <string>

#include <core/common/types/network.hpp>

#include <common/utils/json.hpp>

namespace aos::sm::launcher {

/**
 * Container runtime config.
 */
struct ContainerConfig {
    std::string              mRuntimeDir;
    std::string              mHostWhiteoutsDir;
    std::string              mStorageDir;
    std::string              mStateDir;
    std::vector<std::string> mHostBinds;
    std::vector<Host>        mHosts;
};

/**
 * Parses container runtime config.
 *
 * @param object JSON object.
 * @param workingDir working directory.
 * @param[out] config container runtime config.
 */
void ParseContainerConfig(
    const common::utils::CaseInsensitiveObjectWrapper& object, const std::string& workingDir, ContainerConfig& config);

} // namespace aos::sm::launcher

#endif
