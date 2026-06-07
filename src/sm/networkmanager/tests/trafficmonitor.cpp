/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <chrono>
#include <memory>
#include <thread>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>

#include <sm/networkmanager/trafficmonitor.hpp>

#include <core/sm/tests/mocks/storagemock.hpp>
#include <sm/tests/mocks/firewallbackendmock.hpp>

using namespace aos;
using namespace aos::sm::nftables;
using namespace aos::sm::networkmanager;
using namespace testing;

namespace {

constexpr auto cTable          = "aos-traffic";
constexpr auto cForwardChain   = "forward";
constexpr auto cInputChain     = "input";
constexpr auto cOutputChain    = "output";
constexpr auto cInSystemChain  = "in_system";
constexpr auto cOutSystemChain = "out_system";

FWListedRule MakeCounterRule(uint64_t bytes, FWRuleHandle handle = 1)
{
    FWListedRule r {};

    r.mRule.mCounter = true;
    r.mRule.mAction  = FWActionEnum::eAccept;
    r.mBytes         = bytes;
    r.mHandle        = handle;

    return r;
}

MATCHER_P(JumpTo, target, "")
{
    return arg.mAction == FWActionEnum::eJump && arg.mJumpTarget == target;
}

} // namespace

class TrafficMonitorTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        tests::utils::InitLog();

        mStorage = std::make_unique<NiceMock<StorageMock>>();
        mBackend = std::make_unique<NiceMock<MockFWBackend>>();
        mMonitor = std::make_unique<TrafficMonitor>();
    }

    void TearDown() override
    {
        EXPECT_CALL(*mBackend, NewTxn()).WillOnce(Return(ByMove(MakeTxn())));

        EXPECT_EQ(mMonitor->Stop(), ErrorEnum::eNone);
    }

    std::unique_ptr<NiceMock<MockFWTxn>> MakeTxn()
    {
        auto txn = std::make_unique<NiceMock<MockFWTxn>>();

        ON_CALL(*txn, Commit()).WillByDefault(Return(ErrorEnum::eNone));

        return txn;
    }

    void ExpectInit()
    {
        EXPECT_CALL(*mBackend, NewTxn()).WillOnce(Return(ByMove(MakeTxn()))).WillOnce(Return(ByMove(MakeTxn())));

        EXPECT_CALL(*mStorage, GetTrafficMonitorData(String(cInSystemChain), _, _))
            .WillOnce(Return(ErrorEnum::eNotFound));
        EXPECT_CALL(*mStorage, GetTrafficMonitorData(String(cOutSystemChain), _, _))
            .WillOnce(Return(ErrorEnum::eNotFound));
    }

    std::unique_ptr<NiceMock<StorageMock>>   mStorage;
    std::unique_ptr<NiceMock<MockFWBackend>> mBackend;
    std::unique_ptr<TrafficMonitor>          mMonitor;
};

TEST_F(TrafficMonitorTest, Init)
{
    ExpectInit();

    EXPECT_EQ(mMonitor->Init(*mStorage, *mBackend), ErrorEnum::eNone);
}

TEST_F(TrafficMonitorTest, InitIgnoresMissingTrafficTable)
{
    auto staleTxn = std::make_unique<NiceMock<MockFWTxn>>();

    ON_CALL(*staleTxn, Commit()).WillByDefault(Return(Error(ErrorEnum::eNotFound)));

    EXPECT_CALL(*mBackend, NewTxn()).WillOnce(Return(ByMove(std::move(staleTxn)))).WillOnce(Return(ByMove(MakeTxn())));

    EXPECT_CALL(*mStorage, GetTrafficMonitorData(String(cInSystemChain), _, _)).WillOnce(Return(ErrorEnum::eNotFound));
    EXPECT_CALL(*mStorage, GetTrafficMonitorData(String(cOutSystemChain), _, _)).WillOnce(Return(ErrorEnum::eNotFound));

    EXPECT_EQ(mMonitor->Init(*mStorage, *mBackend), ErrorEnum::eNone);
}

TEST_F(TrafficMonitorTest, StartInstanceMonitoring)
{
    ExpectInit();

    ASSERT_EQ(mMonitor->Init(*mStorage, *mBackend), ErrorEnum::eNone);

    const std::string expectedInChain  = "in_test_instance";
    const std::string expectedOutChain = "out_test_instance";

    auto txn = MakeTxn();
    EXPECT_CALL(*txn, AddChain(_)).Times(2);
    EXPECT_CALL(*txn, AddRule(std::string(cTable), expectedInChain, _)).Times(AtLeast(1));
    EXPECT_CALL(*txn, AddRule(std::string(cTable), expectedOutChain, _)).Times(AtLeast(1));
    EXPECT_CALL(*txn, AddRule(std::string(cTable), std::string(cForwardChain), JumpTo(expectedInChain)));
    EXPECT_CALL(*txn, AddRule(std::string(cTable), std::string(cForwardChain), JumpTo(expectedOutChain)));

    EXPECT_CALL(*mBackend, NewTxn()).WillOnce(Return(ByMove(std::move(txn))));
    EXPECT_CALL(*mStorage, GetTrafficMonitorData(String(expectedInChain.c_str()), _, _))
        .WillOnce(Return(ErrorEnum::eNotFound));
    EXPECT_CALL(*mStorage, GetTrafficMonitorData(String(expectedOutChain.c_str()), _, _))
        .WillOnce(Return(ErrorEnum::eNotFound));

    EXPECT_EQ(mMonitor->StartInstanceMonitoring("test-instance", "192.168.1.100", 1000000, 500000), ErrorEnum::eNone);
}

TEST_F(TrafficMonitorTest, StopInstanceMonitoring)
{
    ExpectInit();
    ASSERT_EQ(mMonitor->Init(*mStorage, *mBackend), ErrorEnum::eNone);

    const std::string expectedInChain  = "in_test_instance";
    const std::string expectedOutChain = "out_test_instance";

    {
        auto txn = MakeTxn();
        EXPECT_CALL(*mBackend, NewTxn()).WillOnce(Return(ByMove(std::move(txn))));
        EXPECT_CALL(*mStorage, GetTrafficMonitorData(String(expectedInChain.c_str()), _, _))
            .WillOnce(Return(ErrorEnum::eNotFound));
        EXPECT_CALL(*mStorage, GetTrafficMonitorData(String(expectedOutChain.c_str()), _, _))
            .WillOnce(Return(ErrorEnum::eNotFound));

        ASSERT_EQ(
            mMonitor->StartInstanceMonitoring("test-instance", "192.168.1.100", 1000000, 500000), ErrorEnum::eNone);
    }

    std::vector<FWListedRule> forwardRules;
    FWListedRule              jumpIn = {};
    jumpIn.mRule.mAction             = FWActionEnum::eJump;
    jumpIn.mRule.mJumpTarget         = expectedInChain;
    jumpIn.mHandle                   = 10;
    FWListedRule jumpOut             = {};
    jumpOut.mRule.mAction            = FWActionEnum::eJump;
    jumpOut.mRule.mJumpTarget        = expectedOutChain;
    jumpOut.mHandle                  = 11;
    forwardRules                     = {jumpIn, jumpOut};

    auto txn = MakeTxn();
    EXPECT_CALL(*txn, DeleteRuleByHandle(std::string(cTable), std::string(cForwardChain), FWRuleHandle {10}));
    EXPECT_CALL(*txn, DeleteRuleByHandle(std::string(cTable), std::string(cForwardChain), FWRuleHandle {11}));
    EXPECT_CALL(*txn, FlushChain(std::string(cTable), expectedInChain));
    EXPECT_CALL(*txn, FlushChain(std::string(cTable), expectedOutChain));
    EXPECT_CALL(*txn, DeleteChain(std::string(cTable), expectedInChain));
    EXPECT_CALL(*txn, DeleteChain(std::string(cTable), expectedOutChain));

    EXPECT_CALL(*mBackend, ListChainRules(std::string(cTable), std::string(cForwardChain), _))
        .WillOnce(DoAll(SetArgReferee<2>(forwardRules), Return(ErrorEnum::eNone)));
    EXPECT_CALL(*mBackend, NewTxn()).WillOnce(Return(ByMove(std::move(txn))));

    EXPECT_CALL(*mStorage, SetTrafficMonitorData(String(expectedInChain.c_str()), _, _))
        .WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mStorage, SetTrafficMonitorData(String(expectedOutChain.c_str()), _, _))
        .WillOnce(Return(ErrorEnum::eNone));

    EXPECT_EQ(mMonitor->StopInstanceMonitoring("test-instance"), ErrorEnum::eNone);
}

TEST_F(TrafficMonitorTest, GetSystemData)
{
    ExpectInit();
    ASSERT_EQ(mMonitor->Init(*mStorage, *mBackend, Time::cSeconds), ErrorEnum::eNone);

    EXPECT_CALL(*mBackend, ListChainRules(std::string(cTable), std::string(cInSystemChain), _))
        .WillOnce(DoAll(SetArgReferee<2>(std::vector<FWListedRule> {MakeCounterRule(200)}), Return(ErrorEnum::eNone)))
        .WillRepeatedly(
            DoAll(SetArgReferee<2>(std::vector<FWListedRule> {MakeCounterRule(300)}), Return(ErrorEnum::eNone)));

    EXPECT_CALL(*mBackend, ListChainRules(std::string(cTable), std::string(cOutSystemChain), _))
        .WillOnce(DoAll(SetArgReferee<2>(std::vector<FWListedRule> {MakeCounterRule(400)}), Return(ErrorEnum::eNone)))
        .WillRepeatedly(
            DoAll(SetArgReferee<2>(std::vector<FWListedRule> {MakeCounterRule(600)}), Return(ErrorEnum::eNone)));

    ASSERT_EQ(mMonitor->Start(), ErrorEnum::eNone);

    std::this_thread::sleep_for(std::chrono::seconds(3));

    uint64_t inputTraffic = 0, outputTraffic = 0;

    EXPECT_EQ(mMonitor->GetSystemTraffic(inputTraffic, outputTraffic), ErrorEnum::eNone);
    EXPECT_EQ(inputTraffic, 100u);
    EXPECT_EQ(outputTraffic, 200u);
}

TEST_F(TrafficMonitorTest, GetInstanceTraffic)
{
    ExpectInit();
    ASSERT_EQ(mMonitor->Init(*mStorage, *mBackend, Time::cSeconds), ErrorEnum::eNone);

    const std::string expectedInChain  = "in_test_instance";
    const std::string expectedOutChain = "out_test_instance";

    {
        auto txn = MakeTxn();
        EXPECT_CALL(*mBackend, NewTxn()).WillOnce(Return(ByMove(std::move(txn))));
        EXPECT_CALL(*mStorage, GetTrafficMonitorData(String(expectedInChain.c_str()), _, _))
            .WillOnce(Return(ErrorEnum::eNotFound));
        EXPECT_CALL(*mStorage, GetTrafficMonitorData(String(expectedOutChain.c_str()), _, _))
            .WillOnce(Return(ErrorEnum::eNotFound));

        ASSERT_EQ(
            mMonitor->StartInstanceMonitoring("test-instance", "192.168.1.100", 1000000, 500000), ErrorEnum::eNone);
    }

    EXPECT_CALL(*mBackend, ListChainRules(std::string(cTable), std::string(cInSystemChain), _))
        .WillRepeatedly(
            DoAll(SetArgReferee<2>(std::vector<FWListedRule> {MakeCounterRule(300)}), Return(ErrorEnum::eNone)));

    EXPECT_CALL(*mBackend, ListChainRules(std::string(cTable), std::string(cOutSystemChain), _))
        .WillRepeatedly(
            DoAll(SetArgReferee<2>(std::vector<FWListedRule> {MakeCounterRule(600)}), Return(ErrorEnum::eNone)));

    EXPECT_CALL(*mBackend, ListChainRules(std::string(cTable), expectedInChain, _))
        .WillOnce(DoAll(SetArgReferee<2>(std::vector<FWListedRule> {MakeCounterRule(200)}), Return(ErrorEnum::eNone)))
        .WillRepeatedly(
            DoAll(SetArgReferee<2>(std::vector<FWListedRule> {MakeCounterRule(400)}), Return(ErrorEnum::eNone)));

    EXPECT_CALL(*mBackend, ListChainRules(std::string(cTable), expectedOutChain, _))
        .WillOnce(DoAll(SetArgReferee<2>(std::vector<FWListedRule> {MakeCounterRule(200)}), Return(ErrorEnum::eNone)))
        .WillRepeatedly(
            DoAll(SetArgReferee<2>(std::vector<FWListedRule> {MakeCounterRule(600)}), Return(ErrorEnum::eNone)));

    ASSERT_EQ(mMonitor->Start(), ErrorEnum::eNone);

    std::this_thread::sleep_for(std::chrono::seconds(3));

    uint64_t inputTraffic = 0, outputTraffic = 0;

    EXPECT_EQ(mMonitor->GetInstanceTraffic("test-instance", inputTraffic, outputTraffic), ErrorEnum::eNone);
    EXPECT_EQ(inputTraffic, 200u);
    EXPECT_EQ(outputTraffic, 400u);

    EXPECT_EQ(mMonitor->GetInstanceTraffic("non-existent", inputTraffic, outputTraffic), ErrorEnum::eNotFound);
}
