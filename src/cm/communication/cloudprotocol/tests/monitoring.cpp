/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>

#include <cm/communication/cloudprotocol/monitoring.hpp>
#include <common/utils/json.hpp>

using namespace testing;

namespace aos::cm::communication::cloudprotocol {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

namespace {

const auto cTime = Time::Unix(1706702400); // 2024-01-31T12:00:00Z

}

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class CloudProtocolMonitoring : public Test {
public:
    void SetUp() override { tests::utils::InitLog(); }
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

void AddMonitoringData(const Time& time, double cpu, size_t ram, size_t download, size_t upload,
    const std::vector<std::pair<std::string, size_t>>& partitions, MonitoringDataArray& items)
{
    auto err = items.EmplaceBack();
    ASSERT_TRUE(err.IsNone()) << "Error: " << tests::utils::ErrorToStr(err);

    items.Back().mTimestamp = time;
    items.Back().mCPU       = cpu;
    items.Back().mRAM       = ram;
    items.Back().mDownload  = download;
    items.Back().mUpload    = upload;

    for (const auto& partition : partitions) {
        auto err = items.Back().mPartitions.EmplaceBack();
        ASSERT_TRUE(err.IsNone()) << "Error: " << tests::utils::ErrorToStr(err);

        items.Back().mPartitions.Back().mName     = partition.first.c_str();
        items.Back().mPartitions.Back().mUsedSize = partition.second;
    }
}

void AddNodeStateInfo(const Time& time, bool provisioned, NodeState state, NodeStateInfoArray& states)
{
    auto err = states.EmplaceBack();
    ASSERT_TRUE(err.IsNone()) << "Error: " << tests::utils::ErrorToStr(err);

    states.Back().mTimestamp   = time;
    states.Back().mProvisioned = provisioned;
    states.Back().mState       = state;
}

void AddInstanceStateInfo(const Time& time, InstanceState state, InstanceStateInfoArray& states)
{
    auto err = states.EmplaceBack();
    ASSERT_TRUE(err.IsNone()) << "Error: " << tests::utils::ErrorToStr(err);

    states.Back().mTimestamp = time;
    states.Back().mState     = state;
}

TEST_F(CloudProtocolMonitoring, Monitoring)
{
    constexpr auto cJSON = R"({"messageType":"monitoringData","nodes":[{"node":{"id":"node1"},"nodeStates":[)"
                           R"({"timestamp":"2024-01-31T12:00:00Z","provisioned":true,"state":"online"},)"
                           R"({"timestamp":"2024-01-31T12:01:00Z","provisioned":true,"state":"offline"}],)"
                           R"("items":[{"timestamp":"2024-01-31T12:00:00Z","ram":2048,"cpu":10,"download":1000,)"
                           R"("upload":500,"partitions":[{"name":"partition1","usedSize":100000}]},)"
                           R"({"timestamp":"2024-01-31T12:01:00Z","ram":2048,"cpu":11,"download":1000,)"
                           R"("upload":500}]},{"node":{"id":"node2"},"nodeStates":[)"
                           R"({"timestamp":"2024-01-31T12:00:00Z","provisioned":false,"state":"error"}],)"
                           R"("items":[]}],"instances":[{"item":{"id":"instance1"},"subject":{"id":"subject1"},)"
                           R"("instance":0,"node":{"id":"node1"},"itemStates":[)"
                           R"({"timestamp":"2024-01-31T12:00:00Z","state":"active"},)"
                           R"({"timestamp":"2024-01-31T12:01:00Z","state":"failed"}],)"
                           R"("items":[{"timestamp":"2024-01-31T12:00:00Z","ram":4096,"cpu":20,"download":2000,)"
                           R"("upload":1000,"partitions":[{"name":"partition1","usedSize":200000}]},)"
                           R"({"timestamp":"2024-01-31T12:01:00Z","ram":4096,"cpu":21,"download":2000,)"
                           R"("upload":1000,"partitions":[{"name":"partition1","usedSize":210000}]}]}]})";

    auto monitoring = std::make_unique<Monitoring>();

    monitoring->mNodes.EmplaceBack();
    monitoring->mNodes.Back().mNodeID = "node1";

    AddMonitoringData(cTime, 10, 2048, 1000, 500, {{"partition1", 100000}}, monitoring->mNodes.Back().mItems);

    AddNodeStateInfo(cTime, true, NodeStateEnum::eOnline, monitoring->mNodes.Back().mStates);

    AddMonitoringData(cTime.Add(Time::cMinutes), 11, 2048, 1000, 500, {}, monitoring->mNodes.Back().mItems);

    AddNodeStateInfo(cTime.Add(Time::cMinutes), true, NodeStateEnum::eOffline, monitoring->mNodes.Back().mStates);

    monitoring->mNodes.EmplaceBack();
    monitoring->mNodes.Back().mNodeID = "node2";

    AddNodeStateInfo(cTime, false, NodeStateEnum::eError, monitoring->mNodes.Back().mStates);

    monitoring->mInstances.EmplaceBack();
    monitoring->mInstances.Back().mNodeID    = "node1";
    monitoring->mInstances.Back().mItemID    = "instance1";
    monitoring->mInstances.Back().mSubjectID = "subject1";
    monitoring->mInstances.Back().mInstance  = 0;

    AddMonitoringData(cTime, 20, 4096, 2000, 1000, {{"partition1", 200000}}, monitoring->mInstances.Back().mItems);

    AddInstanceStateInfo(cTime, InstanceStateEnum::eActive, monitoring->mInstances.Back().mStates);

    AddMonitoringData(cTime.Add(Time::cMinutes), 21, 4096, 2000, 1000, {{"partition1", 210000}},
        monitoring->mInstances.Back().mItems);

    AddInstanceStateInfo(cTime.Add(Time::cMinutes), InstanceStateEnum::eFailed, monitoring->mInstances.Back().mStates);

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(*monitoring, *json);
    ASSERT_TRUE(err.IsNone()) << "Error: " << tests::utils::ErrorToStr(err);

    EXPECT_EQ(common::utils::Stringify(json), cJSON);
}

TEST_F(CloudProtocolMonitoring, MonitoringNoInstances)
{
    constexpr auto cJSON = R"({"messageType":"monitoringData","nodes":[{"node":{"id":"node1"},"nodeStates":[)"
                           R"({"timestamp":"2024-01-31T12:00:00Z","provisioned":true,"state":"online"},)"
                           R"({"timestamp":"2024-01-31T12:01:00Z","provisioned":true,"state":"offline"}],)"
                           R"("items":[{"timestamp":"2024-01-31T12:00:00Z","ram":2048,"cpu":10,"download":1000,)"
                           R"("upload":500,"partitions":[{"name":"partition1","usedSize":100000}]},)"
                           R"({"timestamp":"2024-01-31T12:01:00Z","ram":2048,"cpu":11,"download":1000,)"
                           R"("upload":500}]},{"node":{"id":"node2"},"nodeStates":[)"
                           R"({"timestamp":"2024-01-31T12:00:00Z","provisioned":false,"state":"error"}],)"
                           R"("items":[]}]})";

    auto monitoring = std::make_unique<Monitoring>();

    monitoring->mNodes.EmplaceBack();
    monitoring->mNodes.Back().mNodeID = "node1";

    AddMonitoringData(cTime, 10, 2048, 1000, 500, {{"partition1", 100000}}, monitoring->mNodes.Back().mItems);

    AddNodeStateInfo(cTime, true, NodeStateEnum::eOnline, monitoring->mNodes.Back().mStates);

    AddMonitoringData(cTime.Add(Time::cMinutes), 11, 2048, 1000, 500, {}, monitoring->mNodes.Back().mItems);

    AddNodeStateInfo(cTime.Add(Time::cMinutes), true, NodeStateEnum::eOffline, monitoring->mNodes.Back().mStates);

    monitoring->mNodes.EmplaceBack();
    monitoring->mNodes.Back().mNodeID = "node2";

    AddNodeStateInfo(cTime, false, NodeStateEnum::eError, monitoring->mNodes.Back().mStates);

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(*monitoring, *json);
    ASSERT_TRUE(err.IsNone()) << "Error: " << tests::utils::ErrorToStr(err);

    EXPECT_EQ(common::utils::Stringify(json), cJSON);
}

} // namespace aos::cm::communication::cloudprotocol
