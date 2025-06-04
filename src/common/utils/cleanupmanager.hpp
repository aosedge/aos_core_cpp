/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef UTILS_CLEANUPMANAGER_HPP_
#define UTILS_CLEANUPMANAGER_HPP_

#include <functional>
#include <vector>

namespace aos::common::utils {
/**
 * Cleanup manager.
 */
class CleanupManager {
public:
    /**
     * Adds cleanup.
     */
    void AddCleanup(std::function<void()>&& cleanup);

    /**
     * Executes cleanups.
     */
    void ExecuteCleanups();

private:
    std::vector<std::function<void()>> mCleanups;
};

} // namespace aos::common::utils

#endif // UTILS_CLEANUPMANAGER_HPP_
