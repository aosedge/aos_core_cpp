/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>
#include <core/sm/tests/mocks/networkmanagermock.hpp>

#include <sm/monitoring/resourceusageprovider.hpp>

using namespace testing;

namespace aos::sm::monitoring {

class ResourceUsageProviderTest : public Test {
public:
    void SetUp() override { tests::utils::InitLog(); }

    ResourceUsageProvider              mResourceUsageProvider;
    networkmanager::NetworkManagerMock mNetworkManager;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(ResourceUsageProviderTest, GetNodeMonitoringData)
{
    aos::monitoring::MonitoringData monitoringData;

    ASSERT_TRUE(mResourceUsageProvider.Init(mNetworkManager).IsNone());

    monitoringData.mPartitions.PushBack({});
    monitoringData.mPartitions[0].mName = "root";
    monitoringData.mPartitions[0].mPath = "/";

    auto err = mResourceUsageProvider.GetNodeMonitoringData("nodeID", monitoringData);
    ASSERT_TRUE(err.IsNone());

    ASSERT_GT(monitoringData.mCPU, 0);
    ASSERT_GT(monitoringData.mRAM, 0);

    ASSERT_EQ(monitoringData.mPartitions.Size(), 1);
    ASSERT_GT(monitoringData.mPartitions[0].mUsedSize, 0);
}

TEST_F(ResourceUsageProviderTest, GetInstanceMonitoringData)
{
    aos::monitoring::InstanceMonitoringData monitoringData;

    ASSERT_TRUE(mResourceUsageProvider.Init(mNetworkManager).IsNone());

    auto err = mResourceUsageProvider.GetInstanceMonitoringData("unknown instance", monitoringData);
    ASSERT_TRUE(err.Is(ErrorEnum::eNotFound));
}

} // namespace aos::sm::monitoring
