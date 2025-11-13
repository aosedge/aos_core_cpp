/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <memory>
#include <sstream>
#include <thread>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>

#include <sm/networkmanager/trafficmonitor.hpp>

#include <common/tests/mocks/iptablesmock.hpp>
#include <core/sm/tests/mocks/storagemock.hpp>

using namespace testing;

class TrafficMonitorTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        aos::tests::utils::InitLog();

        mStorage  = std::make_unique<StrictMock<aos::sm::networkmanager::StorageMock>>();
        mIPTables = std::make_unique<StrictMock<aos::common::network::MockIPTables>>();
        mMonitor  = std::make_unique<aos::sm::networkmanager::TrafficMonitor>();
    }

    void TearDown() override
    {
        EXPECT_CALL(*mIPTables, ListChains()).WillOnce(Return(std::vector<std::string> {}));

        EXPECT_EQ(mMonitor->Stop(), aos::ErrorEnum::eNone);
    }

    std::unique_ptr<aos::sm::networkmanager::StorageMock>    mStorage;
    std::unique_ptr<aos::common::network::MockIPTables>      mIPTables;
    std::unique_ptr<aos::sm::networkmanager::TrafficMonitor> mMonitor;
};

TEST_F(TrafficMonitorTest, Init)
{
    EXPECT_CALL(*mIPTables, ListChains()).WillOnce(Return(std::vector<std::string> {}));

    EXPECT_CALL(*mIPTables, NewChain("AOS_SYSTEM_IN")).WillOnce(Return(aos::ErrorEnum::eNone));
    EXPECT_CALL(*mIPTables, NewChain("AOS_SYSTEM_OUT")).WillOnce(Return(aos::ErrorEnum::eNone));

    EXPECT_CALL(*mIPTables, Insert("INPUT", 1, _)).WillOnce(Return(aos::ErrorEnum::eNone));
    EXPECT_CALL(*mIPTables, Insert("OUTPUT", 1, _)).WillOnce(Return(aos::ErrorEnum::eNone));

    EXPECT_CALL(*mIPTables, Append("AOS_SYSTEM_IN", _)).Times(2).WillRepeatedly(Return(aos::ErrorEnum::eNone));

    EXPECT_CALL(*mIPTables, Append("AOS_SYSTEM_OUT", _)).Times(2).WillRepeatedly(Return(aos::ErrorEnum::eNone));

    aos::String inChain  = "AOS_SYSTEM_IN";
    aos::String outChain = "AOS_SYSTEM_OUT";

    EXPECT_CALL(*mStorage, GetTrafficMonitorData(inChain, _, _)).WillOnce(Return(aos::ErrorEnum::eNotFound));
    EXPECT_CALL(*mStorage, GetTrafficMonitorData(outChain, _, _)).WillOnce(Return(aos::ErrorEnum::eNotFound));

    EXPECT_EQ(mMonitor->Init(*mStorage, *mIPTables), aos::ErrorEnum::eNone);
}

TEST_F(TrafficMonitorTest, StartInstanceMonitoring)
{
    EXPECT_CALL(*mIPTables, ListChains()).WillOnce(Return(std::vector<std::string> {}));
    EXPECT_CALL(*mIPTables, NewChain(_)).Times(2).WillRepeatedly(Return(aos::ErrorEnum::eNone));
    EXPECT_CALL(*mIPTables, Insert("INPUT", 1, _)).WillOnce(Return(aos::ErrorEnum::eNone));
    EXPECT_CALL(*mIPTables, Insert("OUTPUT", 1, _)).WillOnce(Return(aos::ErrorEnum::eNone));
    EXPECT_CALL(*mIPTables, Append("AOS_SYSTEM_IN", _)).Times(2).WillRepeatedly(Return(aos::ErrorEnum::eNone));
    EXPECT_CALL(*mIPTables, Append("AOS_SYSTEM_OUT", _)).Times(2).WillRepeatedly(Return(aos::ErrorEnum::eNone));

    aos::String inChain  = "AOS_SYSTEM_IN";
    aos::String outChain = "AOS_SYSTEM_OUT";

    EXPECT_CALL(*mStorage, GetTrafficMonitorData(inChain, _, _)).WillOnce(Return(aos::ErrorEnum::eNotFound));
    EXPECT_CALL(*mStorage, GetTrafficMonitorData(outChain, _, _)).WillOnce(Return(aos::ErrorEnum::eNotFound));

    ASSERT_EQ(mMonitor->Init(*mStorage, *mIPTables), aos::ErrorEnum::eNone);

    std::stringstream ss;
    ss << std::hex << std::hash<std::string> {}("test-instance");
    std::string chainBase = ss.str();

    std::string expectedInChain  = "AOS_" + chainBase + "_IN";
    std::string expectedOutChain = "AOS_" + chainBase + "_OUT";

    EXPECT_CALL(*mIPTables, NewChain(expectedInChain)).WillOnce(Return(aos::ErrorEnum::eNone));
    EXPECT_CALL(*mIPTables, NewChain(expectedOutChain)).WillOnce(Return(aos::ErrorEnum::eNone));
    EXPECT_CALL(*mIPTables, Insert("FORWARD", 1, _)).Times(2).WillRepeatedly(Return(aos::ErrorEnum::eNone));
    EXPECT_CALL(*mIPTables, Append(expectedInChain, _)).Times(2).WillRepeatedly(Return(aos::ErrorEnum::eNone));
    EXPECT_CALL(*mIPTables, Append(expectedOutChain, _)).Times(2).WillRepeatedly(Return(aos::ErrorEnum::eNone));

    aos::String inInstanceChain  = expectedInChain.c_str();
    aos::String outInstanceChain = expectedOutChain.c_str();
    EXPECT_CALL(*mStorage, GetTrafficMonitorData(inInstanceChain, _, _)).WillOnce(Return(aos::ErrorEnum::eNotFound));
    EXPECT_CALL(*mStorage, GetTrafficMonitorData(outInstanceChain, _, _)).WillOnce(Return(aos::ErrorEnum::eNotFound));

    EXPECT_EQ(
        mMonitor->StartInstanceMonitoring("test-instance", "192.168.1.100", 1000000, 500000), aos::ErrorEnum::eNone);
}

TEST_F(TrafficMonitorTest, GetSystemData)
{
    EXPECT_CALL(*mIPTables, ListChains()).WillOnce(Return(std::vector<std::string> {}));
    EXPECT_CALL(*mIPTables, NewChain("AOS_SYSTEM_IN")).WillOnce(Return(aos::ErrorEnum::eNone));
    EXPECT_CALL(*mIPTables, NewChain("AOS_SYSTEM_OUT")).WillOnce(Return(aos::ErrorEnum::eNone));
    EXPECT_CALL(*mIPTables, Insert("INPUT", 1, _)).WillOnce(Return(aos::ErrorEnum::eNone));
    EXPECT_CALL(*mIPTables, Insert("OUTPUT", 1, _)).WillOnce(Return(aos::ErrorEnum::eNone));
    EXPECT_CALL(*mIPTables, Append("AOS_SYSTEM_IN", _)).Times(2).WillRepeatedly(Return(aos::ErrorEnum::eNone));
    EXPECT_CALL(*mIPTables, Append("AOS_SYSTEM_OUT", _)).Times(2).WillRepeatedly(Return(aos::ErrorEnum::eNone));

    aos::String inChain  = "AOS_SYSTEM_IN";
    aos::String outChain = "AOS_SYSTEM_OUT";

    EXPECT_CALL(*mStorage, GetTrafficMonitorData(inChain, _, _)).WillOnce(Return(aos::ErrorEnum::eNotFound));
    EXPECT_CALL(*mStorage, GetTrafficMonitorData(outChain, _, _)).WillOnce(Return(aos::ErrorEnum::eNotFound));

    ASSERT_EQ(mMonitor->Init(*mStorage, *mIPTables, aos::Time::cSeconds), aos::ErrorEnum::eNone);

    EXPECT_CALL(*mIPTables, ListAllRulesWithCounters("AOS_SYSTEM_IN"))
        .WillOnce(Return(std::vector<std::string> {"-c 100 200"}))
        .WillRepeatedly(Return(std::vector<std::string> {"-c 300 300"}));

    EXPECT_CALL(*mIPTables, ListAllRulesWithCounters("AOS_SYSTEM_OUT"))
        .WillOnce(Return(std::vector<std::string> {"-c 100 400"}))
        .WillRepeatedly(Return(std::vector<std::string> {"-c 300 600"}));

    EXPECT_CALL(*mStorage, SetTrafficMonitorData(_, _, _))
        .Times(AnyNumber())
        .WillRepeatedly(Return(aos::ErrorEnum::eNone));

    ASSERT_EQ(mMonitor->Start(), aos::ErrorEnum::eNone);

    std::this_thread::sleep_for(std::chrono::seconds(3));

    uint64_t inputTraffic = 0, outputTraffic = 0;
    EXPECT_EQ(mMonitor->GetSystemData(inputTraffic, outputTraffic), aos::ErrorEnum::eNone);
    EXPECT_EQ(inputTraffic, 100);
    EXPECT_EQ(outputTraffic, 200);
}

TEST_F(TrafficMonitorTest, StopInstanceMonitoring)
{
    EXPECT_CALL(*mIPTables, ListChains()).WillOnce(Return(std::vector<std::string> {}));
    EXPECT_CALL(*mIPTables, NewChain("AOS_SYSTEM_IN")).WillOnce(Return(aos::ErrorEnum::eNone));
    EXPECT_CALL(*mIPTables, NewChain("AOS_SYSTEM_OUT")).WillOnce(Return(aos::ErrorEnum::eNone));
    EXPECT_CALL(*mIPTables, Insert("INPUT", 1, _)).WillOnce(Return(aos::ErrorEnum::eNone));
    EXPECT_CALL(*mIPTables, Insert("OUTPUT", 1, _)).WillOnce(Return(aos::ErrorEnum::eNone));
    EXPECT_CALL(*mIPTables, Append("AOS_SYSTEM_IN", _)).Times(2).WillRepeatedly(Return(aos::ErrorEnum::eNone));
    EXPECT_CALL(*mIPTables, Append("AOS_SYSTEM_OUT", _)).Times(2).WillRepeatedly(Return(aos::ErrorEnum::eNone));

    aos::String inChain  = "AOS_SYSTEM_IN";
    aos::String outChain = "AOS_SYSTEM_OUT";

    EXPECT_CALL(*mStorage, GetTrafficMonitorData(inChain, _, _)).WillOnce(Return(aos::ErrorEnum::eNotFound));
    EXPECT_CALL(*mStorage, GetTrafficMonitorData(outChain, _, _)).WillOnce(Return(aos::ErrorEnum::eNotFound));

    ASSERT_EQ(mMonitor->Init(*mStorage, *mIPTables), aos::ErrorEnum::eNone);

    std::stringstream ss;
    ss << std::hex << std::hash<std::string> {}("test-instance");
    std::string chainBase        = ss.str();
    std::string expectedInChain  = "AOS_" + chainBase + "_IN";
    std::string expectedOutChain = "AOS_" + chainBase + "_OUT";

    EXPECT_CALL(*mIPTables, NewChain(expectedInChain)).WillOnce(Return(aos::ErrorEnum::eNone));
    EXPECT_CALL(*mIPTables, NewChain(expectedOutChain)).WillOnce(Return(aos::ErrorEnum::eNone));
    EXPECT_CALL(*mIPTables, Insert("FORWARD", 1, _)).Times(2).WillRepeatedly(Return(aos::ErrorEnum::eNone));
    EXPECT_CALL(*mIPTables, Append(expectedInChain, _)).Times(2).WillRepeatedly(Return(aos::ErrorEnum::eNone));
    EXPECT_CALL(*mIPTables, Append(expectedOutChain, _)).Times(2).WillRepeatedly(Return(aos::ErrorEnum::eNone));

    aos::String inInstanceChain  = expectedInChain.c_str();
    aos::String outInstanceChain = expectedOutChain.c_str();

    EXPECT_CALL(*mStorage, GetTrafficMonitorData(inInstanceChain, _, _)).WillOnce(Return(aos::ErrorEnum::eNotFound));
    EXPECT_CALL(*mStorage, GetTrafficMonitorData(outInstanceChain, _, _)).WillOnce(Return(aos::ErrorEnum::eNotFound));

    ASSERT_EQ(
        mMonitor->StartInstanceMonitoring("test-instance", "192.168.1.100", 1000000, 500000), aos::ErrorEnum::eNone);

    EXPECT_CALL(*mIPTables, DeleteRule("FORWARD", _)).Times(2).WillRepeatedly(Return(aos::ErrorEnum::eNone));

    EXPECT_CALL(*mIPTables, ClearChain(expectedInChain)).WillOnce(Return(aos::ErrorEnum::eNone));
    EXPECT_CALL(*mIPTables, ClearChain(expectedOutChain)).WillOnce(Return(aos::ErrorEnum::eNone));

    EXPECT_CALL(*mIPTables, DeleteChain(expectedInChain)).WillOnce(Return(aos::ErrorEnum::eNone));
    EXPECT_CALL(*mIPTables, DeleteChain(expectedOutChain)).WillOnce(Return(aos::ErrorEnum::eNone));

    EXPECT_CALL(*mStorage, SetTrafficMonitorData(inInstanceChain, _, _)).WillOnce(Return(aos::ErrorEnum::eNone));
    EXPECT_CALL(*mStorage, SetTrafficMonitorData(outInstanceChain, _, _)).WillOnce(Return(aos::ErrorEnum::eNone));

    EXPECT_EQ(mMonitor->StopInstanceMonitoring("test-instance"), aos::ErrorEnum::eNone);
}

TEST_F(TrafficMonitorTest, GetInstanceTraffic)
{
    EXPECT_CALL(*mIPTables, ListChains()).WillOnce(Return(std::vector<std::string> {}));
    EXPECT_CALL(*mIPTables, NewChain("AOS_SYSTEM_IN")).WillOnce(Return(aos::ErrorEnum::eNone));
    EXPECT_CALL(*mIPTables, NewChain("AOS_SYSTEM_OUT")).WillOnce(Return(aos::ErrorEnum::eNone));
    EXPECT_CALL(*mIPTables, Insert("INPUT", 1, _)).WillOnce(Return(aos::ErrorEnum::eNone));
    EXPECT_CALL(*mIPTables, Insert("OUTPUT", 1, _)).WillOnce(Return(aos::ErrorEnum::eNone));
    EXPECT_CALL(*mIPTables, Append("AOS_SYSTEM_IN", _)).Times(2).WillRepeatedly(Return(aos::ErrorEnum::eNone));
    EXPECT_CALL(*mIPTables, Append("AOS_SYSTEM_OUT", _)).Times(2).WillRepeatedly(Return(aos::ErrorEnum::eNone));

    aos::String inChain  = "AOS_SYSTEM_IN";
    aos::String outChain = "AOS_SYSTEM_OUT";

    EXPECT_CALL(*mStorage, GetTrafficMonitorData(inChain, _, _)).WillOnce(Return(aos::ErrorEnum::eNotFound));
    EXPECT_CALL(*mStorage, GetTrafficMonitorData(outChain, _, _)).WillOnce(Return(aos::ErrorEnum::eNotFound));

    ASSERT_EQ(mMonitor->Init(*mStorage, *mIPTables, aos::Time::cSeconds), aos::ErrorEnum::eNone);

    std::stringstream ss;
    ss << std::hex << std::hash<std::string> {}("test-instance");
    std::string chainBase        = ss.str();
    std::string expectedInChain  = "AOS_" + chainBase + "_IN";
    std::string expectedOutChain = "AOS_" + chainBase + "_OUT";

    EXPECT_CALL(*mIPTables, NewChain(expectedInChain)).WillOnce(Return(aos::ErrorEnum::eNone));
    EXPECT_CALL(*mIPTables, NewChain(expectedOutChain)).WillOnce(Return(aos::ErrorEnum::eNone));
    EXPECT_CALL(*mIPTables, Insert("FORWARD", 1, _)).Times(2).WillRepeatedly(Return(aos::ErrorEnum::eNone));
    EXPECT_CALL(*mIPTables, Append(expectedInChain, _)).Times(2).WillRepeatedly(Return(aos::ErrorEnum::eNone));
    EXPECT_CALL(*mIPTables, Append(expectedOutChain, _)).Times(2).WillRepeatedly(Return(aos::ErrorEnum::eNone));

    aos::String inInstanceChain  = expectedInChain.c_str();
    aos::String outInstanceChain = expectedOutChain.c_str();

    EXPECT_CALL(*mStorage, GetTrafficMonitorData(inInstanceChain, _, _)).WillOnce(Return(aos::ErrorEnum::eNotFound));
    EXPECT_CALL(*mStorage, GetTrafficMonitorData(outInstanceChain, _, _)).WillOnce(Return(aos::ErrorEnum::eNotFound));

    ASSERT_EQ(
        mMonitor->StartInstanceMonitoring("test-instance", "192.168.1.100", 1000000, 500000), aos::ErrorEnum::eNone);

    EXPECT_CALL(*mIPTables, ListAllRulesWithCounters(inChain.CStr()))
        .WillRepeatedly(Return(std::vector<std::string> {"-c 300 400"}));

    EXPECT_CALL(*mIPTables, ListAllRulesWithCounters(outChain.CStr()))
        .WillRepeatedly(Return(std::vector<std::string> {"-c 300 600"}));

    EXPECT_CALL(*mIPTables, ListAllRulesWithCounters(expectedInChain))
        .WillOnce(Return(std::vector<std::string> {"-c 100 200"}))
        .WillRepeatedly(Return(std::vector<std::string> {"-c 300 400"}));

    EXPECT_CALL(*mIPTables, ListAllRulesWithCounters(expectedOutChain))
        .WillOnce(Return(std::vector<std::string> {"-c 100 200"}))
        .WillRepeatedly(Return(std::vector<std::string> {"-c 300 600"}));

    EXPECT_CALL(*mStorage, SetTrafficMonitorData(_, _, _))
        .Times(AnyNumber())
        .WillRepeatedly(Return(aos::ErrorEnum::eNone));

    ASSERT_EQ(mMonitor->Start(), aos::ErrorEnum::eNone);

    std::this_thread::sleep_for(std::chrono::seconds(3));

    uint64_t inputTraffic = 0, outputTraffic = 0;
    EXPECT_EQ(mMonitor->GetInstanceTraffic("test-instance", inputTraffic, outputTraffic), aos::ErrorEnum::eNone);
    EXPECT_EQ(inputTraffic, 200);
    EXPECT_EQ(outputTraffic, 400);

    EXPECT_EQ(mMonitor->GetInstanceTraffic("non-existent", inputTraffic, outputTraffic), aos::ErrorEnum::eNotFound);
}
