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

TEST_F(TrafficMonitorTest, BatchStagesInstancesIntoSingleCommit)
{
    ExpectInit();
    ASSERT_EQ(mMonitor->Init(*mStorage, *mBackend), ErrorEnum::eNone);

    auto txn = MakeTxn();
    EXPECT_CALL(*txn, AddChain(_)).Times(4);
    EXPECT_CALL(*txn, AddRule(std::string(cTable), std::string("in_inst1"), _)).Times(AtLeast(1));
    EXPECT_CALL(*txn, AddRule(std::string(cTable), std::string("out_inst1"), _)).Times(AtLeast(1));
    EXPECT_CALL(*txn, AddRule(std::string(cTable), std::string("in_inst2"), _)).Times(AtLeast(1));
    EXPECT_CALL(*txn, AddRule(std::string(cTable), std::string("out_inst2"), _)).Times(AtLeast(1));
    EXPECT_CALL(*txn, AddRule(std::string(cTable), std::string(cForwardChain), JumpTo(std::string("in_inst1"))));
    EXPECT_CALL(*txn, AddRule(std::string(cTable), std::string(cForwardChain), JumpTo(std::string("out_inst1"))));
    EXPECT_CALL(*txn, AddRule(std::string(cTable), std::string(cForwardChain), JumpTo(std::string("in_inst2"))));
    EXPECT_CALL(*txn, AddRule(std::string(cTable), std::string(cForwardChain), JumpTo(std::string("out_inst2"))));
    EXPECT_CALL(*txn, Commit()).Times(0);
    EXPECT_CALL(*txn, Commit(An<std::vector<FWListedRule>&>())).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_CALL(*mBackend, NewTxn()).WillOnce(Return(ByMove(std::move(txn))));
    EXPECT_CALL(*mStorage, GetTrafficMonitorData(_, _, _)).WillRepeatedly(Return(ErrorEnum::eNotFound));

    ASSERT_EQ(mMonitor->BeginBatch(), ErrorEnum::eNone);
    ASSERT_EQ(mMonitor->StartInstanceMonitoring("inst1", "192.168.1.100", 0, 0), ErrorEnum::eNone);
    ASSERT_EQ(mMonitor->StartInstanceMonitoring("inst2", "192.168.1.101", 0, 0), ErrorEnum::eNone);

    EXPECT_EQ(mMonitor->FlushBatch(), ErrorEnum::eNone);
}

TEST_F(TrafficMonitorTest, BatchStagesStopInstanceDeletes)
{
    ExpectInit();
    ASSERT_EQ(mMonitor->Init(*mStorage, *mBackend), ErrorEnum::eNone);

    const std::string inChain  = "in_test_instance";
    const std::string outChain = "out_test_instance";

    auto startTxn = MakeTxn();
    auto batchTxn = MakeTxn();

    auto* batchPtr = batchTxn.get();

    EXPECT_CALL(*mBackend, NewTxn())
        .WillOnce(Return(ByMove(std::move(startTxn))))
        .WillOnce(Return(ByMove(std::move(batchTxn))));
    EXPECT_CALL(*mStorage, GetTrafficMonitorData(_, _, _)).WillRepeatedly(Return(ErrorEnum::eNotFound));

    ASSERT_EQ(mMonitor->StartInstanceMonitoring("test-instance", "192.168.1.100", 0, 0), ErrorEnum::eNone);

    std::vector<FWListedRule> forwardRules;
    forwardRules.push_back({{"", "", "", 0, "", FWActionEnum::eJump, inChain}, FWRuleHandle {10}});
    forwardRules.push_back({{"", "", "", 0, "", FWActionEnum::eJump, outChain}, FWRuleHandle {11}});

    EXPECT_CALL(*mBackend, ListChainRules(std::string(cTable), std::string(cForwardChain), _))
        .WillOnce(DoAll(SetArgReferee<2>(forwardRules), Return(ErrorEnum::eNone)));
    EXPECT_CALL(*batchPtr, DeleteRuleByHandle(std::string(cTable), std::string(cForwardChain), FWRuleHandle {10}));
    EXPECT_CALL(*batchPtr, DeleteRuleByHandle(std::string(cTable), std::string(cForwardChain), FWRuleHandle {11}));
    EXPECT_CALL(*batchPtr, FlushChain(std::string(cTable), inChain));
    EXPECT_CALL(*batchPtr, DeleteChain(std::string(cTable), inChain));
    EXPECT_CALL(*batchPtr, FlushChain(std::string(cTable), outChain));
    EXPECT_CALL(*batchPtr, DeleteChain(std::string(cTable), outChain));
    EXPECT_CALL(*batchPtr, Commit()).Times(0);
    EXPECT_CALL(*batchPtr, Commit(An<std::vector<FWListedRule>&>())).WillOnce(Return(ErrorEnum::eNone));

    ASSERT_EQ(mMonitor->BeginBatch(), ErrorEnum::eNone);
    ASSERT_EQ(mMonitor->StopInstanceMonitoring("test-instance"), ErrorEnum::eNone);

    EXPECT_EQ(mMonitor->FlushBatch(), ErrorEnum::eNone);
}

TEST_F(TrafficMonitorTest, RevertDeletesFlushedHandlesAndClearsInstanceState)
{
    ExpectInit();
    ASSERT_EQ(mMonitor->Init(*mStorage, *mBackend), ErrorEnum::eNone);

    const std::string inChain  = "in_inst1";
    const std::string outChain = "out_inst1";

    auto batchTxn  = MakeTxn();
    auto revertTxn = MakeTxn();
    auto retryTxn  = MakeTxn();

    auto* batchPtr  = batchTxn.get();
    auto* revertPtr = revertTxn.get();
    auto* retryPtr  = retryTxn.get();

    EXPECT_CALL(*mBackend, NewTxn())
        .WillOnce(Return(ByMove(std::move(batchTxn))))
        .WillOnce(Return(ByMove(std::move(revertTxn))))
        .WillOnce(Return(ByMove(std::move(retryTxn))));
    EXPECT_CALL(*mStorage, GetTrafficMonitorData(_, _, _)).WillRepeatedly(Return(ErrorEnum::eNotFound));
    EXPECT_CALL(*mStorage, SetTrafficMonitorData(_, _, _)).Times(0);

    EXPECT_CALL(*batchPtr, Commit(An<std::vector<FWListedRule>&>()))
        .WillOnce([inChain, outChain](std::vector<FWListedRule>& added) {
            added = {
                {{"", "", "", 0, "", FWActionEnum::eJump, inChain}, FWRuleHandle {20}},
                {{"", "", "", 0, "", FWActionEnum::eJump, outChain}, FWRuleHandle {21}},
            };

            return Error(ErrorEnum::eNone);
        });

    ASSERT_EQ(mMonitor->BeginBatch(), ErrorEnum::eNone);
    ASSERT_EQ(mMonitor->StartInstanceMonitoring("inst1", "192.168.1.100", 0, 0), ErrorEnum::eNone);
    ASSERT_EQ(mMonitor->FlushBatch(), ErrorEnum::eNone);

    std::vector<FWListedRule> forwardRules;
    forwardRules.push_back({{"", "", "", 0, "", FWActionEnum::eJump, inChain}, FWRuleHandle {20}});
    forwardRules.push_back({{"", "", "", 0, "", FWActionEnum::eJump, outChain}, FWRuleHandle {21}});
    forwardRules.push_back({{"", "", "", 0, "", FWActionEnum::eJump, "in_other"}, FWRuleHandle {9}});

    EXPECT_CALL(*mBackend, ListChainRules(std::string(cTable), std::string(cForwardChain), _))
        .WillOnce(DoAll(SetArgReferee<2>(forwardRules), Return(ErrorEnum::eNone)));
    EXPECT_CALL(*revertPtr, DeleteRuleByHandle(std::string(cTable), std::string(cForwardChain), FWRuleHandle {20}));
    EXPECT_CALL(*revertPtr, DeleteRuleByHandle(std::string(cTable), std::string(cForwardChain), FWRuleHandle {21}));
    EXPECT_CALL(*revertPtr, FlushChain(std::string(cTable), inChain));
    EXPECT_CALL(*revertPtr, DeleteChain(std::string(cTable), inChain));
    EXPECT_CALL(*revertPtr, FlushChain(std::string(cTable), outChain));
    EXPECT_CALL(*revertPtr, DeleteChain(std::string(cTable), outChain));

    ASSERT_EQ(mMonitor->Revert(), ErrorEnum::eNone);

    uint64_t inputTraffic = 0, outputTraffic = 0;

    EXPECT_EQ(mMonitor->GetInstanceTraffic("inst1", inputTraffic, outputTraffic), ErrorEnum::eNotFound);

    // The instance is unknown again, so the per-instance retry really re-applies.
    EXPECT_CALL(*retryPtr, AddChain(_)).Times(2);
    EXPECT_CALL(*retryPtr, AddRule(std::string(cTable), inChain, _)).Times(AtLeast(1));
    EXPECT_CALL(*retryPtr, AddRule(std::string(cTable), outChain, _)).Times(AtLeast(1));
    EXPECT_CALL(*retryPtr, AddRule(std::string(cTable), std::string(cForwardChain), JumpTo(inChain)));
    EXPECT_CALL(*retryPtr, AddRule(std::string(cTable), std::string(cForwardChain), JumpTo(outChain)));

    EXPECT_EQ(mMonitor->StartInstanceMonitoring("inst1", "192.168.1.100", 0, 0), ErrorEnum::eNone);
}

TEST_F(TrafficMonitorTest, FailedFlushClearsStagedInstanceState)
{
    ExpectInit();
    ASSERT_EQ(mMonitor->Init(*mStorage, *mBackend), ErrorEnum::eNone);

    const std::string inChain  = "in_inst1";
    const std::string outChain = "out_inst1";

    auto batchTxn = MakeTxn();
    auto retryTxn = MakeTxn();

    auto* batchPtr = batchTxn.get();
    auto* retryPtr = retryTxn.get();

    EXPECT_CALL(*mBackend, NewTxn())
        .WillOnce(Return(ByMove(std::move(batchTxn))))
        .WillOnce(Return(ByMove(std::move(retryTxn))));
    EXPECT_CALL(*mStorage, GetTrafficMonitorData(_, _, _)).WillRepeatedly(Return(ErrorEnum::eNotFound));

    EXPECT_CALL(*batchPtr, Commit(An<std::vector<FWListedRule>&>())).WillOnce(Return(Error(ErrorEnum::eFailed)));

    ASSERT_EQ(mMonitor->BeginBatch(), ErrorEnum::eNone);
    ASSERT_EQ(mMonitor->StartInstanceMonitoring("inst1", "192.168.1.100", 0, 0), ErrorEnum::eNone);

    EXPECT_FALSE(mMonitor->FlushBatch().IsNone());

    // Nothing was applied, so the per-instance retry must build and commit its
    // own transaction instead of returning early for a known instance.
    EXPECT_CALL(*retryPtr, AddChain(_)).Times(2);
    EXPECT_CALL(*retryPtr, AddRule(std::string(cTable), inChain, _)).Times(AtLeast(1));
    EXPECT_CALL(*retryPtr, AddRule(std::string(cTable), outChain, _)).Times(AtLeast(1));
    EXPECT_CALL(*retryPtr, AddRule(std::string(cTable), std::string(cForwardChain), JumpTo(inChain)));
    EXPECT_CALL(*retryPtr, AddRule(std::string(cTable), std::string(cForwardChain), JumpTo(outChain)));
    EXPECT_CALL(*retryPtr, Commit()).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_EQ(mMonitor->StartInstanceMonitoring("inst1", "192.168.1.100", 0, 0), ErrorEnum::eNone);
}

TEST_F(TrafficMonitorTest, FlushBatchAndRevertWithoutBeginAreNoOp)
{
    ExpectInit();
    ASSERT_EQ(mMonitor->Init(*mStorage, *mBackend), ErrorEnum::eNone);

    EXPECT_CALL(*mBackend, ListChainRules(_, _, _)).Times(0);

    EXPECT_EQ(mMonitor->FlushBatch(), ErrorEnum::eNone);
    EXPECT_EQ(mMonitor->Revert(), ErrorEnum::eNone);
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
