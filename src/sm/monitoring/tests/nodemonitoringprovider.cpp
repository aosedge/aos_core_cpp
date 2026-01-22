/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <core/common/iamclient/itf/currentnodeinfoprovider.hpp>
#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>
#include <core/sm/tests/mocks/networkmanagermock.hpp>

#include <sm/monitoring/nodemonitoringprovider.hpp>

using namespace testing;

namespace aos::sm::monitoring {

namespace {

class TrafficProviderMock : public networkmanager::SystemTrafficProviderItf {
public:
    MOCK_METHOD(Error, GetSystemTraffic, (size_t&, size_t&), (const, override));
};

class CurrentNodeInfoProviderMock : public iamclient::CurrentNodeInfoProviderItf {
public:
    MOCK_METHOD(Error, GetCurrentNodeInfo, (NodeInfo & nodeInfo), (const, override));
    MOCK_METHOD(Error, SubscribeListener, (iamclient::CurrentNodeInfoListenerItf & listener), (override));
    MOCK_METHOD(Error, UnsubscribeListener, (iamclient::CurrentNodeInfoListenerItf & listener), (override));
};

} // namespace

class ResourceUsageProviderTest : public Test {
public:
    void SetUp() override
    {
        tests::utils::InitLog();

        mNodeInfo.mPartitions.EmplaceBack();
        mNodeInfo.mPartitions.Back().mName = "root";
        mNodeInfo.mPartitions.Back().mPath = "/";

        EXPECT_CALL(mNodeInfoProvider, GetCurrentNodeInfo)
            .WillOnce(DoAll(SetArgReferee<0>(mNodeInfo), Return(ErrorEnum::eNone)));
    }

    NodeMonitoringProvider      mNodeMonitoringProvider;
    TrafficProviderMock         mTrafficProvider;
    CurrentNodeInfoProviderMock mNodeInfoProvider;
    NodeInfo                    mNodeInfo;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(ResourceUsageProviderTest, GetNodeMonitoringData)
{
    auto monitoringData = std::make_unique<MonitoringData>();
    auto partitionInfos = std::make_unique<PartitionInfoArray>();

    auto err = mNodeMonitoringProvider.Init(mNodeInfoProvider, mTrafficProvider);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = mNodeMonitoringProvider.Start();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_CALL(mTrafficProvider, GetSystemTraffic)
        .WillOnce(DoAll(SetArgReferee<0>(1024), SetArgReferee<1>(2048), Return(ErrorEnum::eNone)));

    err = mNodeMonitoringProvider.GetNodeMonitoringData(*monitoringData);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(monitoringData->mDownload, 1024);
    EXPECT_EQ(monitoringData->mUpload, 2048);

    ASSERT_GT(monitoringData->mCPU, 0);
    ASSERT_GT(monitoringData->mRAM, 0);

    ASSERT_EQ(monitoringData->mPartitions.Size(), 1);
    ASSERT_GT(monitoringData->mPartitions[0].mUsedSize, 0);

    err = mNodeMonitoringProvider.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

} // namespace aos::sm::monitoring
