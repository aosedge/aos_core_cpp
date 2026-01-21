/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>

#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>

#include <sm/launcher/runtimes/boot/partitionmanager.hpp>

using namespace testing;

namespace aos::sm::launcher {

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class PartitionManagerTest : public Test {
protected:
    static void SetUpTestSuite() { tests::utils::InitLog(); }

    PartitionManager mPartitionManager;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(PartitionManagerTest, GetPartInfo)
{
    PartInfo partInfo;

    auto err = mPartitionManager.GetPartInfo("/dev/nvme1n1p3", partInfo);
    EXPECT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(partInfo.mDevice, "/dev/nvme1n1p3");
    EXPECT_EQ(partInfo.mParentDevice, "/dev/nvme1n1");
    EXPECT_EQ(partInfo.mPartitionNumber, 3);
}

} // namespace aos::sm::launcher
