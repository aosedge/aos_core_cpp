/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_UTILS_UTILS_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_UTILS_UTILS_HPP_

#include <string>

#include <core/common/types/common.hpp>

namespace aos::sm::launcher::utils {

/**
 * Creates runtime info.
 *
 * @param runtimeType runtime type.
 * @param nodeInfo node info.
 * @param maxInstances max number of instances.
 * @param[out] runtimeInfo runtime info.
 * @return Error.
 */
Error CreateRuntimeInfo(
    const std::string& runtimeType, const NodeInfo& nodeInfo, size_t maxInstances, RuntimeInfo& runtimeInfo);

} // namespace aos::sm::launcher::utils

#endif
