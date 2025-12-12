/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_ROOTFS_CONFIG_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_ROOTFS_CONFIG_HPP_

#include <string>
#include <vector>

#include <core/common/types/common.hpp>

namespace aos::sm::launcher::rootfs {

/**
 * Rootfs runtime config.
 */
struct Config {
    RuntimeInfo              mRuntimeInfo;
    std::string              mCurrentInstanceFile;
    std::string              mCurrentVersionFile;
    std::string              mPendingInstanceFile;
    std::string              mUpdateDir;
    std::vector<std::string> mUpdateServicesToCheck;
    Duration                 mServiceStartTimeout {};
};

} // namespace aos::sm::launcher::rootfs

#endif
