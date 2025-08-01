/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <sstream>

#include <Poco/JSON/Object.h>

#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>

#include <cm/monitoring/monitoring.hpp>

using namespace testing;

namespace aos::cm::monitoring {

namespace {

std::unique_ptr<aos::monitoring::NodeMonitoringData> CreateNodeMonitoringData(
    const String& nodeID, const Time& timestamp)
{
    auto monitoring = std::make_unique<aos::monitoring::NodeMonitoringData>();

    monitoring->mNodeID    = nodeID;
    monitoring->mTimestamp = timestamp;

    return monitoring;
}

class GetMonitoring : public StaticVisitor<const cloudprotocol::Monitoring*> {
public:
    Res Visit(const cloudprotocol::Monitoring& monitoring) const { return &monitoring; }

    template <typename T>
    Res Visit(const T&) const
    {
        return nullptr;
    }
};

class CommunicationStub : public communication::CommunicationItf {
public:
    Error SendMessage(const cloudprotocol::MessageVariant& body) override
    {
        std::lock_guard lock {mMutex};

        LOG_DBG() << "Send message called";

        mMessages.push_back(body);
        mCondVar.notify_all();

        return ErrorEnum::eNone;
    }

    bool WaitForMessage(
        cloudprotocol::MessageVariant& message, std::chrono::milliseconds timeout = std::chrono::seconds(5))
    {
        std::unique_lock lock {mMutex};

        if (!mCondVar.wait_for(lock, timeout, [&]() { return !mMessages.empty(); })) {
            return false;
        }

        message = std::move(mMessages.front());
        mMessages.erase(mMessages.begin());

        return true;
    }

    std::vector<cloudprotocol::MessageVariant> GetMessages()
    {
        std::lock_guard lock {mMutex};

        return mMessages;
    }

private:
    std::mutex                                 mMutex;
    std::condition_variable                    mCondVar;
    std::vector<cloudprotocol::MessageVariant> mMessages;
};

} // namespace

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class CMAlerts : public Test {
protected:
    void SetUp() override
    {
        tests::utils::InitLog();

        mConfig.mSendPeriod         = Time::cSeconds * 1;
        mConfig.mMaxMessageSize     = 2 * 1024;
        mConfig.mMaxOfflineMessages = 1;

        auto err = mMonitoring.Init(mConfig, mCommunication);
        ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
    }

    config::Monitoring mConfig {};
    CommunicationStub  mCommunication;
    Monitoring         mMonitoring;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(CMAlerts, SendMonitoring)
{
    auto err = mMonitoring.Start();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    auto nodeMonitoring = CreateNodeMonitoringData("node1", Time::Now());

    nodeMonitoring->mMonitoringData.mCPU = 50.0;
    nodeMonitoring->mMonitoringData.mRAM = 1024 * 4;

    nodeMonitoring->mServiceInstances.EmplaceBack();
    nodeMonitoring->mServiceInstances[0].mInstanceIdent       = InstanceIdent {"service1", "subject1", 1};
    nodeMonitoring->mServiceInstances[0].mMonitoringData.mCPU = 20.0;

    nodeMonitoring->mServiceInstances[0].mMonitoringData.mPartitions.EmplaceBack();
    nodeMonitoring->mServiceInstances[0].mMonitoringData.mPartitions[0].mName     = "partition1";
    nodeMonitoring->mServiceInstances[0].mMonitoringData.mPartitions[0].mUsedSize = 512.0;

    err = mMonitoring.SendMonitoringData(*nodeMonitoring);
    EXPECT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    mMonitoring.OnConnect();

    auto msg = std::make_unique<cloudprotocol::MessageVariant>();

    EXPECT_TRUE(mCommunication.WaitForMessage(*msg));

    const cloudprotocol::Monitoring* monitoring = msg ? msg->ApplyVisitor(GetMonitoring()) : nullptr;
    EXPECT_NE(monitoring, nullptr) << "Monitoring data is not received";

    if (monitoring) {
        EXPECT_EQ(monitoring->mNodes.Size(), 1);
        EXPECT_EQ(monitoring->mNodes[0].mNodeID, "node1");
        EXPECT_EQ(monitoring->mNodes[0].mItems.Size(), 1);
        EXPECT_EQ(monitoring->mNodes[0].mItems[0].mCPU, 50.0);
        EXPECT_EQ(monitoring->mNodes[0].mItems[0].mRAM, 1024 * 4);

        EXPECT_EQ(monitoring->mServiceInstances.Size(), 1);

        const InstanceIdent instanceIdent {"service1", "subject1", 1};
        EXPECT_EQ(monitoring->mServiceInstances[0].mInstanceIdent, instanceIdent);
        EXPECT_EQ(monitoring->mServiceInstances[0].mItems.Size(), 1);

        EXPECT_EQ(monitoring->mServiceInstances[0].mItems[0].mCPU, 20.0);

        EXPECT_EQ(monitoring->mServiceInstances[0].mItems[0].mPartitions.Size(), 1);
        EXPECT_EQ(monitoring->mServiceInstances[0].mItems[0].mPartitions[0].mName, "partition1");
        EXPECT_EQ(monitoring->mServiceInstances[0].mItems[0].mPartitions[0].mUsedSize, 512.0);
    }

    err = mMonitoring.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(CMAlerts, SendMonitoringOfflineMessagesAreLimited)
{
    auto err = mMonitoring.Start();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    auto nodeMonitoring = CreateNodeMonitoringData("node1", Time::Now());

    nodeMonitoring->mMonitoringData.mCPU = 50.0;
    nodeMonitoring->mMonitoringData.mRAM = 1024 * 4;

    nodeMonitoring->mServiceInstances.EmplaceBack();
    nodeMonitoring->mServiceInstances[0].mInstanceIdent       = InstanceIdent {"service1", "subject1", 1};
    nodeMonitoring->mServiceInstances[0].mMonitoringData.mCPU = 20.0;

    nodeMonitoring->mServiceInstances[0].mMonitoringData.mPartitions.EmplaceBack();
    nodeMonitoring->mServiceInstances[0].mMonitoringData.mPartitions[0].mName     = "partition1";
    nodeMonitoring->mServiceInstances[0].mMonitoringData.mPartitions[0].mUsedSize = 512.0;

    for (size_t i = 0; i < cloudprotocol::cMonitoringItemsCount + 1; ++i) {
        err = mMonitoring.SendMonitoringData(*nodeMonitoring);
        EXPECT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
    }

    mMonitoring.OnConnect();

    auto msg = std::make_unique<cloudprotocol::MessageVariant>();

    EXPECT_TRUE(mCommunication.WaitForMessage(*msg));

    const cloudprotocol::Monitoring* monitoring = msg ? msg->ApplyVisitor(GetMonitoring()) : nullptr;
    EXPECT_NE(monitoring, nullptr) << "Monitoring data is not received";

    if (monitoring) {
        EXPECT_EQ(monitoring->mNodes.Size(), 1);
        EXPECT_EQ(monitoring->mNodes[0].mNodeID, "node1");
        EXPECT_EQ(monitoring->mNodes[0].mItems.Size(), 1);
        EXPECT_EQ(monitoring->mNodes[0].mItems[0].mCPU, 50.0);
        EXPECT_EQ(monitoring->mNodes[0].mItems[0].mRAM, 1024 * 4);

        EXPECT_EQ(monitoring->mServiceInstances.Size(), 1);

        const InstanceIdent instanceIdent {"service1", "subject1", 1};
        EXPECT_EQ(monitoring->mServiceInstances[0].mInstanceIdent, instanceIdent);
        EXPECT_EQ(monitoring->mServiceInstances[0].mItems.Size(), 1);

        EXPECT_EQ(monitoring->mServiceInstances[0].mItems[0].mCPU, 20.0);

        EXPECT_EQ(monitoring->mServiceInstances[0].mItems[0].mPartitions.Size(), 1);
        EXPECT_EQ(monitoring->mServiceInstances[0].mItems[0].mPartitions[0].mName, "partition1");
        EXPECT_EQ(monitoring->mServiceInstances[0].mItems[0].mPartitions[0].mUsedSize, 512.0);
    }

    err = mMonitoring.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

} // namespace aos::cm::monitoring
