/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cleanupmanager.hpp"

namespace aos::common::utils {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

void CleanupManager::AddCleanup(std::function<void()>&& cleanup)
{
    mCleanups.push_back(std::move(cleanup));
}

void CleanupManager::ExecuteCleanups()
{
    for (auto it = mCleanups.rbegin(); it != mCleanups.rend(); ++it) {
        (*it)();
    }
}

} // namespace aos::common::utils
