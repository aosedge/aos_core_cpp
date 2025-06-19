/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <common/utils/cleanupmanager.hpp>

namespace aos::common::utils::test {

class CleanupManagerTest : public ::testing::Test {
protected:
    CleanupManager mCleanupManager;
};

TEST_F(CleanupManagerTest, SingleCleanupExecuted)
{
    bool cleanupExecuted = false;

    mCleanupManager.AddCleanup([&cleanupExecuted]() { cleanupExecuted = true; });

    mCleanupManager.ExecuteCleanups();

    EXPECT_TRUE(cleanupExecuted);
}

TEST_F(CleanupManagerTest, MultipleCleanupsExecutedInReverseOrder)
{
    std::vector<int> executionOrder;

    mCleanupManager.AddCleanup([&executionOrder]() { executionOrder.push_back(1); });

    mCleanupManager.AddCleanup([&executionOrder]() { executionOrder.push_back(2); });

    mCleanupManager.AddCleanup([&executionOrder]() { executionOrder.push_back(3); });

    mCleanupManager.ExecuteCleanups();

    ASSERT_EQ(executionOrder.size(), 3);
    EXPECT_EQ(executionOrder[0], 3);
    EXPECT_EQ(executionOrder[1], 2);
    EXPECT_EQ(executionOrder[2], 1);
}

} // namespace aos::common::utils::test
