/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>

#include <sm/networkmanager/firewall.hpp>
#include <sm/tests/mocks/firewallbackendmock.hpp>

using namespace aos;
using namespace aos::sm::nftables;
using namespace aos::sm::networkmanager;
using namespace testing;

/***********************************************************************************************************************
 * Helpers
 **********************************************************************************************************************/

namespace {

InputAccessConfig MakeInput(const char* port, const char* proto)
{
    InputAccessConfig c;

    c.mPort     = port;
    c.mProtocol = proto;

    return c;
}

OutputAccessConfig MakeOutput(const char* dstIP, const char* dstPort, const char* proto, const char* srcIP = "")
{
    OutputAccessConfig c;

    c.mDstIP   = dstIP;
    c.mDstPort = dstPort;
    c.mProto   = proto;
    c.mSrcIP   = srcIP;

    return c;
}

InstanceFirewallParams MakeParams(const char* ip, bool allowPublic)
{
    InstanceFirewallParams p;

    p.mIP          = ip;
    p.mAllowPublic = allowPublic;

    return p;
}

MATCHER_P(BaseChainNamed, name, "")
{
    return arg.mName == name;
}

MATCHER_P(ChainNamed, name, "")
{
    return arg.mName == name;
}

MATCHER_P4(InputRule, dstAddr, proto, port, action, "")
{
    return arg.mDstAddr == dstAddr && arg.mProto == proto && arg.mDstPort == port && arg.mDstPortEnd == 0
        && arg.mAction == action && arg.mSrcAddr.empty();
}

MATCHER_P5(InputRangeRule, dstAddr, proto, portFrom, portTo, action, "")
{
    return arg.mDstAddr == dstAddr && arg.mProto == proto && arg.mDstPort == portFrom && arg.mDstPortEnd == portTo
        && arg.mAction == action && arg.mSrcAddr.empty();
}

MATCHER_P5(OutputRule, srcAddr, dstIP, proto, port, action, "")
{
    return arg.mSrcAddr == srcAddr && arg.mDstAddr == dstIP && arg.mProto == proto && arg.mDstPort == port
        && arg.mDstPortEnd == 0 && arg.mAction == action;
}

MATCHER_P6(OutputRangeRule, srcAddr, dstIP, proto, portFrom, portTo, action, "")
{
    return arg.mSrcAddr == srcAddr && arg.mDstAddr == dstIP && arg.mProto == proto && arg.mDstPort == portFrom
        && arg.mDstPortEnd == portTo && arg.mAction == action;
}

MATCHER_P2(TerminalInRule, dstAddr, action, "")
{
    return arg.mAction == action && arg.mDstAddr == dstAddr && arg.mSrcAddr.empty() && arg.mProto.empty()
        && arg.mDstPort == 0 && arg.mOIFName.empty();
}

MATCHER_P2(TerminalOutRule, srcAddr, action, "")
{
    return arg.mAction == action && arg.mSrcAddr == srcAddr && arg.mDstAddr.empty() && arg.mProto.empty()
        && arg.mDstPort == 0 && arg.mOIFName.empty();
}

MATCHER_P2(SameNetRule, srcAddr, dstAddr, "")
{
    return arg.mAction == FWActionEnum::eAccept && arg.mSrcAddr == srcAddr && arg.mDstAddr == dstAddr
        && arg.mProto.empty() && arg.mDstPort == 0 && arg.mOIFName.empty();
}

MATCHER_P2(JumpRule, addrField, target, "")
{
    return arg.mAction == FWActionEnum::eJump && arg.mJumpTarget == target && addrField(arg);
}

MATCHER_P2(MasqueradeRule, subnet, oifname, "")
{
    return arg.mAction == FWActionEnum::eMasquerade && arg.mSrcAddr == subnet && arg.mOIFName == oifname && arg.mOIFNeg;
}

MATCHER_P(BaseChainPolicy, policy, "")
{
    return arg.mPolicy == policy;
}

} // namespace

/***********************************************************************************************************************
 * Fixture
 **********************************************************************************************************************/

class FirewallTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        aos::tests::utils::InitLog();

        ASSERT_TRUE(mFirewall.Init(mBackend).IsNone());
    }

    std::unique_ptr<StrictMock<MockFWTxn>> NewMockTx()
    {
        auto tx = std::make_unique<StrictMock<MockFWTxn>>();

        mTxnPtr = tx.get();

        return tx;
    }

    StrictMock<MockFWBackend> mBackend;
    Firewall                  mFirewall;
    StrictMock<MockFWTxn>*    mTxnPtr {};
};

/***********************************************************************************************************************
 * Start / Stop
 **********************************************************************************************************************/

TEST_F(FirewallTest, StartAdoptsExistingTableWithoutRecreating)
{
    // Provisioned forward chain: ct rules only, no instance jumps.
    std::vector<FWListedRule> forwardRules;
    forwardRules.push_back({{"", "", "", 0, 0, "", FWActionEnum::eDrop, ""}, FWRuleHandle {1}});
    forwardRules.push_back({{"", "", "", 0, 0, "", FWActionEnum::eAccept, ""}, FWRuleHandle {2}});

    EXPECT_CALL(mBackend, ListChainRules(_, std::string("forward"), _))
        .WillOnce(DoAll(SetArgReferee<2>(forwardRules), Return(ErrorEnum::eNone)));
    EXPECT_CALL(mBackend, NewTxn()).Times(0);

    EXPECT_TRUE(mFirewall.Start().IsNone());
}

TEST_F(FirewallTest, StartKeepsRulesLeftByPreviousLifetime)
{
    std::vector<FWListedRule> forwardRules;
    forwardRules.push_back({{"10.0.0.5", "", "", 0, 0, "", FWActionEnum::eJump, "instance_alive"}, FWRuleHandle {30}});

    EXPECT_CALL(mBackend, ListChainRules(_, std::string("forward"), _))
        .WillOnce(DoAll(SetArgReferee<2>(forwardRules), Return(ErrorEnum::eNone)));
    EXPECT_CALL(mBackend, NewTxn()).Times(0);

    EXPECT_TRUE(mFirewall.Start().IsNone());
}

/***********************************************************************************************************************
 * RemoveOrphans
 **********************************************************************************************************************/

TEST_F(FirewallTest, RemoveOrphansDropsUnknownInstanceChainsAndKeepsKnownOnes)
{
    std::vector<FWListedRule> forwardRules;
    forwardRules.push_back({{"", "", "", 0, 0, "", FWActionEnum::eDrop, ""}, FWRuleHandle {1}});
    forwardRules.push_back({{"10.0.0.5", "", "", 0, 0, "", FWActionEnum::eJump, "instance_alive"}, FWRuleHandle {20}});
    forwardRules.push_back({{"10.0.0.6", "", "", 0, 0, "", FWActionEnum::eJump, "instance_stale"}, FWRuleHandle {30}});
    forwardRules.push_back({{"", "10.0.0.6", "", 0, 0, "", FWActionEnum::eJump, "instance_stale"}, FWRuleHandle {31}});

    std::vector<FWListedRule> postRules;

    auto tx = NewMockTx();

    InSequence seq;
    EXPECT_CALL(mBackend, ListChainRules(_, std::string("forward"), _))
        .WillOnce(DoAll(SetArgReferee<2>(forwardRules), Return(ErrorEnum::eNone)));
    EXPECT_CALL(mBackend, ListChainRules(_, std::string("postrouting"), _))
        .WillOnce(DoAll(SetArgReferee<2>(postRules), Return(ErrorEnum::eNone)));
    EXPECT_CALL(mBackend, NewTxn()).WillOnce(Return(ByMove(std::move(tx))));
    EXPECT_CALL(*mTxnPtr, DeleteRuleByHandle(_, std::string("forward"), FWRuleHandle {30}));
    EXPECT_CALL(*mTxnPtr, DeleteRuleByHandle(_, std::string("forward"), FWRuleHandle {31}));
    EXPECT_CALL(*mTxnPtr, FlushChain(_, std::string("instance_stale")));
    EXPECT_CALL(*mTxnPtr, DeleteChain(_, std::string("instance_stale")));
    EXPECT_CALL(*mTxnPtr, Commit()).WillOnce(Return(ErrorEnum::eNone));

    StaticArray<StaticString<cIDLen>, 2> knownInstances;
    knownInstances.PushBack("alive");

    StaticArray<MasqueradeParams, 2> knownMasquerades;

    EXPECT_TRUE(mFirewall.RemoveOrphans(knownInstances, knownMasquerades).IsNone());
}

TEST_F(FirewallTest, RemoveOrphansDropsUnknownMasqueradeOnly)
{
    std::vector<FWListedRule> forwardRules;

    std::vector<FWListedRule> postRules;
    postRules.push_back(
        {{"10.0.0.0/24", "", "", 0, 0, "br-known", FWActionEnum::eMasquerade, "", false, "", true}, FWRuleHandle {40}});
    postRules.push_back(
        {{"10.0.1.0/24", "", "", 0, 0, "br-gone", FWActionEnum::eMasquerade, "", false, "", true}, FWRuleHandle {41}});

    auto tx = NewMockTx();

    InSequence seq;
    EXPECT_CALL(mBackend, ListChainRules(_, std::string("forward"), _))
        .WillOnce(DoAll(SetArgReferee<2>(forwardRules), Return(ErrorEnum::eNone)));
    EXPECT_CALL(mBackend, ListChainRules(_, std::string("postrouting"), _))
        .WillOnce(DoAll(SetArgReferee<2>(postRules), Return(ErrorEnum::eNone)));
    EXPECT_CALL(mBackend, NewTxn()).WillOnce(Return(ByMove(std::move(tx))));
    EXPECT_CALL(*mTxnPtr, DeleteRuleByHandle(_, std::string("postrouting"), FWRuleHandle {41}));
    EXPECT_CALL(*mTxnPtr, Commit()).WillOnce(Return(ErrorEnum::eNone));

    StaticArray<StaticString<cIDLen>, 2> knownInstances;

    StaticArray<MasqueradeParams, 2> knownMasquerades;
    knownMasquerades.PushBack({"10.0.0.0/24", "br-known"});

    EXPECT_TRUE(mFirewall.RemoveOrphans(knownInstances, knownMasquerades).IsNone());
}

TEST_F(FirewallTest, AdoptedMasqueradeIsNotAddedAgain)
{
    std::vector<FWListedRule> forwardRules;

    std::vector<FWListedRule> postRules;
    postRules.push_back(
        {{"10.0.0.0/24", "", "", 0, 0, "br-known", FWActionEnum::eMasquerade, "", false, "", true}, FWRuleHandle {40}});

    EXPECT_CALL(mBackend, ListChainRules(_, std::string("forward"), _))
        .WillOnce(DoAll(SetArgReferee<2>(forwardRules), Return(ErrorEnum::eNone)));
    EXPECT_CALL(mBackend, ListChainRules(_, std::string("postrouting"), _))
        .WillOnce(DoAll(SetArgReferee<2>(postRules), Return(ErrorEnum::eNone)));
    EXPECT_CALL(mBackend, NewTxn()).Times(0);

    StaticArray<StaticString<cIDLen>, 2> knownInstances;

    StaticArray<MasqueradeParams, 2> knownMasquerades;
    knownMasquerades.PushBack({"10.0.0.0/24", "br-known"});

    ASSERT_TRUE(mFirewall.RemoveOrphans(knownInstances, knownMasquerades).IsNone());

    EXPECT_TRUE(mFirewall.AddMasquerade("10.0.0.0/24", "br-known").IsNone());
}

TEST_F(FirewallTest, StartFallbackCreatesSkeletonWithForwardPolicyDrop)
{
    auto tx = NewMockTx();

    InSequence seq;
    EXPECT_CALL(mBackend, ListChainRules(_, std::string("forward"), _)).WillOnce(Return(Error(ErrorEnum::eFailed)));
    EXPECT_CALL(mBackend, NewTxn()).WillOnce(Return(ByMove(std::move(tx))));
    EXPECT_CALL(*mTxnPtr, AddTable(_));
    EXPECT_CALL(*mTxnPtr, AddBaseChain(AllOf(BaseChainNamed("forward"), BaseChainPolicy(FWActionEnum::eDrop))));
    EXPECT_CALL(*mTxnPtr, AddBaseChain(AllOf(BaseChainNamed("postrouting"), BaseChainPolicy(FWActionEnum::eAccept))));
    EXPECT_CALL(*mTxnPtr,
        AddRule(_, std::string("forward"),
            AllOf(Field(&FWRule::mCtState, "invalid"), Field(&FWRule::mAction, FWActionEnum::eDrop))));
    EXPECT_CALL(*mTxnPtr,
        AddRule(_, std::string("forward"),
            AllOf(Field(&FWRule::mCtState, "established,related"), Field(&FWRule::mAction, FWActionEnum::eAccept))));
    EXPECT_CALL(*mTxnPtr, Commit()).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_TRUE(mFirewall.Start().IsNone());
}

TEST_F(FirewallTest, StartFallbackFailsWhenCommitFails)
{
    auto tx = NewMockTx();

    InSequence seq;
    EXPECT_CALL(mBackend, ListChainRules(_, std::string("forward"), _)).WillOnce(Return(Error(ErrorEnum::eFailed)));
    EXPECT_CALL(mBackend, NewTxn()).WillOnce(Return(ByMove(std::move(tx))));
    EXPECT_CALL(*mTxnPtr, AddTable(_));
    EXPECT_CALL(*mTxnPtr, AddBaseChain(_)).Times(2);
    EXPECT_CALL(*mTxnPtr, AddRule(_, std::string("forward"), _)).Times(2);
    EXPECT_CALL(*mTxnPtr, Commit()).WillOnce(Return(Error(ErrorEnum::eFailed)));

    EXPECT_FALSE(mFirewall.Start().IsNone());
}

TEST_F(FirewallTest, StopRemovesArtifactsButKeepsTable)
{
    std::vector<FWListedRule> forwardRules;
    forwardRules.push_back({{"10.0.0.5", "", "", 0, 0, "", FWActionEnum::eJump, "instance_test"}, FWRuleHandle {40}});

    std::vector<FWListedRule> postRules;
    postRules.push_back({{"10.0.0.0/24", "", "", 0, 0, "br-test", FWActionEnum::eMasquerade, ""}, FWRuleHandle {50}});

    auto tx = NewMockTx();

    InSequence seq;
    EXPECT_CALL(mBackend, ListChainRules(_, std::string("forward"), _))
        .WillOnce(DoAll(SetArgReferee<2>(forwardRules), Return(ErrorEnum::eNone)));
    EXPECT_CALL(mBackend, ListChainRules(_, std::string("postrouting"), _))
        .WillOnce(DoAll(SetArgReferee<2>(postRules), Return(ErrorEnum::eNone)));
    EXPECT_CALL(mBackend, NewTxn()).WillOnce(Return(ByMove(std::move(tx))));
    EXPECT_CALL(*mTxnPtr, DeleteRuleByHandle(_, std::string("forward"), FWRuleHandle {40}));
    EXPECT_CALL(*mTxnPtr, FlushChain(_, std::string("instance_test")));
    EXPECT_CALL(*mTxnPtr, DeleteChain(_, std::string("instance_test")));
    EXPECT_CALL(*mTxnPtr, DeleteRuleByHandle(_, std::string("postrouting"), FWRuleHandle {50}));
    EXPECT_CALL(*mTxnPtr, Commit()).WillOnce(Return(ErrorEnum::eNone));

    // No DeleteTable: the base table and policy drop must survive SM shutdown.
    EXPECT_TRUE(mFirewall.Stop().IsNone());
}

TEST_F(FirewallTest, StopWithNoTableIsNoOp)
{
    EXPECT_CALL(mBackend, ListChainRules(_, std::string("forward"), _)).WillOnce(Return(Error(ErrorEnum::eFailed)));
    EXPECT_CALL(mBackend, NewTxn()).Times(0);

    EXPECT_TRUE(mFirewall.Stop().IsNone());
}

/***********************************************************************************************************************
 * AddInstance
 **********************************************************************************************************************/

TEST_F(FirewallTest, AddInstanceInputRulesTranslated)
{
    auto params = MakeParams("10.0.0.5", true);

    ASSERT_TRUE(params.mInput.PushBack(MakeInput("8080", "tcp")).IsNone());
    ASSERT_TRUE(params.mInput.PushBack(MakeInput("53", "udp")).IsNone());

    auto tx = NewMockTx();

    InSequence seq;
    EXPECT_CALL(mBackend, NewTxn()).WillOnce(Return(ByMove(std::move(tx))));
    EXPECT_CALL(*mTxnPtr, AddChain(ChainNamed("instance_test")));
    EXPECT_CALL(*mTxnPtr,
        AddRule(_, std::string("instance_test"),
            InputRule(std::string("10.0.0.5"), std::string("tcp"), 8080, FWActionEnum::eAccept)));
    EXPECT_CALL(*mTxnPtr,
        AddRule(_, std::string("instance_test"),
            InputRule(std::string("10.0.0.5"), std::string("udp"), 53, FWActionEnum::eAccept)));
    EXPECT_CALL(*mTxnPtr,
        AddRule(_, std::string("instance_test"), TerminalInRule(std::string("10.0.0.5"), FWActionEnum::eDrop)));
    EXPECT_CALL(*mTxnPtr,
        AddRule(_, std::string("instance_test"), TerminalOutRule(std::string("10.0.0.5"), FWActionEnum::eAccept)));
    EXPECT_CALL(*mTxnPtr, AddRule(_, std::string("forward"), _)).Times(2);
    EXPECT_CALL(*mTxnPtr, Commit(An<std::vector<FWRuleHandle>&>())).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_TRUE(mFirewall.AddInstance("test", params).IsNone());
}

TEST_F(FirewallTest, AddInstanceOutputRulesTranslated)
{
    auto params = MakeParams("10.0.0.5", true);

    ASSERT_TRUE(params.mOutput.PushBack(MakeOutput("192.168.1.0/24", "443", "tcp")).IsNone());
    ASSERT_TRUE(params.mOutput.PushBack(MakeOutput("8.8.8.8", "53", "udp", "10.0.0.5")).IsNone());

    auto tx = NewMockTx();

    InSequence seq;
    EXPECT_CALL(mBackend, NewTxn()).WillOnce(Return(ByMove(std::move(tx))));
    EXPECT_CALL(*mTxnPtr, AddChain(_));
    EXPECT_CALL(*mTxnPtr,
        AddRule(_, std::string("instance_test"),
            OutputRule(std::string("10.0.0.5"), std::string("192.168.1.0/24"), std::string("tcp"), 443,
                FWActionEnum::eAccept)));
    EXPECT_CALL(*mTxnPtr,
        AddRule(_, std::string("instance_test"),
            OutputRule(
                std::string("10.0.0.5"), std::string("8.8.8.8"), std::string("udp"), 53, FWActionEnum::eAccept)));
    EXPECT_CALL(*mTxnPtr,
        AddRule(_, std::string("instance_test"), TerminalInRule(std::string("10.0.0.5"), FWActionEnum::eDrop)));
    EXPECT_CALL(*mTxnPtr,
        AddRule(_, std::string("instance_test"), TerminalOutRule(std::string("10.0.0.5"), FWActionEnum::eAccept)));
    EXPECT_CALL(*mTxnPtr, AddRule(_, std::string("forward"), _)).Times(2);
    EXPECT_CALL(*mTxnPtr, Commit(An<std::vector<FWRuleHandle>&>())).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_TRUE(mFirewall.AddInstance("test", params).IsNone());
}

TEST_F(FirewallTest, AddInstanceDenyPublicProducesDrop)
{
    auto params = MakeParams("10.0.0.5", false);

    auto tx = NewMockTx();

    InSequence seq;
    EXPECT_CALL(mBackend, NewTxn()).WillOnce(Return(ByMove(std::move(tx))));
    EXPECT_CALL(*mTxnPtr, AddChain(_));
    EXPECT_CALL(*mTxnPtr,
        AddRule(_, std::string("instance_test"), TerminalInRule(std::string("10.0.0.5"), FWActionEnum::eDrop)));
    EXPECT_CALL(*mTxnPtr,
        AddRule(_, std::string("instance_test"), TerminalOutRule(std::string("10.0.0.5"), FWActionEnum::eDrop)));
    EXPECT_CALL(*mTxnPtr, AddRule(_, std::string("forward"), _)).Times(2);
    EXPECT_CALL(*mTxnPtr, Commit(An<std::vector<FWRuleHandle>&>())).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_TRUE(mFirewall.AddInstance("test", params).IsNone());
}

TEST_F(FirewallTest, AddInstanceInstallsBothJumpsInForward)
{
    auto params = MakeParams("10.0.0.5", true);

    auto tx = NewMockTx();

    InSequence seq;
    EXPECT_CALL(mBackend, NewTxn()).WillOnce(Return(ByMove(std::move(tx))));
    EXPECT_CALL(*mTxnPtr, AddChain(_));
    EXPECT_CALL(*mTxnPtr,
        AddRule(_, std::string("instance_test"), TerminalInRule(std::string("10.0.0.5"), FWActionEnum::eDrop)));
    EXPECT_CALL(*mTxnPtr,
        AddRule(_, std::string("instance_test"), TerminalOutRule(std::string("10.0.0.5"), FWActionEnum::eAccept)));

    EXPECT_CALL(*mTxnPtr,
        AddRule(_, std::string("forward"),
            AllOf(Field(&FWRule::mDstAddr, "10.0.0.5"), Field(&FWRule::mSrcAddr, ""),
                Field(&FWRule::mAction, FWActionEnum::eJump), Field(&FWRule::mJumpTarget, "instance_test"))));

    EXPECT_CALL(*mTxnPtr,
        AddRule(_, std::string("forward"),
            AllOf(Field(&FWRule::mSrcAddr, "10.0.0.5"), Field(&FWRule::mDstAddr, ""),
                Field(&FWRule::mAction, FWActionEnum::eJump), Field(&FWRule::mJumpTarget, "instance_test"))));

    EXPECT_CALL(*mTxnPtr, Commit(An<std::vector<FWRuleHandle>&>())).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_TRUE(mFirewall.AddInstance("test", params).IsNone());
}

TEST_F(FirewallTest, AddInstanceSameNetworkAcceptsIntraSubnetBeforeAccessRules)
{
    auto params    = MakeParams("10.0.0.5", true);
    params.mSubnet = "10.0.0.0/24";
    ASSERT_TRUE(params.mInput.PushBack(MakeInput("8080", "tcp")).IsNone());

    auto tx = NewMockTx();

    InSequence seq;
    EXPECT_CALL(mBackend, NewTxn()).WillOnce(Return(ByMove(std::move(tx))));
    EXPECT_CALL(*mTxnPtr, AddChain(ChainNamed("instance_test")));
    // Same-network accepts sit at the top of the instance chain, ahead of the
    // per-instance access rules and the terminal drop.
    EXPECT_CALL(*mTxnPtr,
        AddRule(_, std::string("instance_test"), SameNetRule(std::string("10.0.0.0/24"), std::string("10.0.0.5"))));
    EXPECT_CALL(*mTxnPtr,
        AddRule(_, std::string("instance_test"), SameNetRule(std::string("10.0.0.5"), std::string("10.0.0.0/24"))));
    EXPECT_CALL(*mTxnPtr,
        AddRule(_, std::string("instance_test"),
            InputRule(std::string("10.0.0.5"), std::string("tcp"), 8080, FWActionEnum::eAccept)));
    EXPECT_CALL(*mTxnPtr,
        AddRule(_, std::string("instance_test"), TerminalInRule(std::string("10.0.0.5"), FWActionEnum::eDrop)));
    EXPECT_CALL(*mTxnPtr,
        AddRule(_, std::string("instance_test"), TerminalOutRule(std::string("10.0.0.5"), FWActionEnum::eAccept)));
    EXPECT_CALL(*mTxnPtr, AddRule(_, std::string("forward"), _)).Times(2);
    EXPECT_CALL(*mTxnPtr, Commit(An<std::vector<FWRuleHandle>&>())).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_TRUE(mFirewall.AddInstance("test", params).IsNone());
}

TEST_F(FirewallTest, AddInstanceNoSubnetSkipsSameNetworkAccepts)
{
    // Without a subnet the instance chain keeps the original shape: no
    // same-network accepts, only the terminal verdicts.
    auto params = MakeParams("10.0.0.5", true);

    auto tx = NewMockTx();

    InSequence seq;
    EXPECT_CALL(mBackend, NewTxn()).WillOnce(Return(ByMove(std::move(tx))));
    EXPECT_CALL(*mTxnPtr, AddChain(_));
    EXPECT_CALL(*mTxnPtr,
        AddRule(_, std::string("instance_test"), TerminalInRule(std::string("10.0.0.5"), FWActionEnum::eDrop)));
    EXPECT_CALL(*mTxnPtr,
        AddRule(_, std::string("instance_test"), TerminalOutRule(std::string("10.0.0.5"), FWActionEnum::eAccept)));
    EXPECT_CALL(*mTxnPtr, AddRule(_, std::string("forward"), _)).Times(2);
    EXPECT_CALL(*mTxnPtr, Commit(An<std::vector<FWRuleHandle>&>())).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_TRUE(mFirewall.AddInstance("test", params).IsNone());
}

TEST_F(FirewallTest, AddInstanceSanitisesInstanceID)
{
    auto params = MakeParams("10.0.0.5", true);

    auto tx = NewMockTx();

    InSequence seq;
    EXPECT_CALL(mBackend, NewTxn()).WillOnce(Return(ByMove(std::move(tx))));
    EXPECT_CALL(*mTxnPtr, AddChain(ChainNamed("instance_abc_123_de")));
    EXPECT_CALL(*mTxnPtr, AddRule(_, std::string("instance_abc_123_de"), _)).Times(2);
    EXPECT_CALL(*mTxnPtr, AddRule(_, std::string("forward"), Field(&FWRule::mJumpTarget, "instance_abc_123_de")))
        .Times(2);
    EXPECT_CALL(*mTxnPtr, Commit(An<std::vector<FWRuleHandle>&>())).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_TRUE(mFirewall.AddInstance("abc-123-de", params).IsNone());
}

TEST_F(FirewallTest, AddInstanceDefaultsMissingProtocolToTcp)
{
    auto params = MakeParams("10.0.0.5", true);
    ASSERT_TRUE(params.mInput.PushBack(MakeInput("8080", "")).IsNone());

    auto tx = NewMockTx();

    InSequence seq;
    EXPECT_CALL(mBackend, NewTxn()).WillOnce(Return(ByMove(std::move(tx))));
    EXPECT_CALL(*mTxnPtr, AddChain(_));
    EXPECT_CALL(*mTxnPtr,
        AddRule(_, std::string("instance_test"),
            InputRule(std::string("10.0.0.5"), std::string("tcp"), 8080, FWActionEnum::eAccept)));
    EXPECT_CALL(*mTxnPtr,
        AddRule(_, std::string("instance_test"), TerminalInRule(std::string("10.0.0.5"), FWActionEnum::eDrop)));
    EXPECT_CALL(*mTxnPtr,
        AddRule(_, std::string("instance_test"), TerminalOutRule(std::string("10.0.0.5"), FWActionEnum::eAccept)));
    EXPECT_CALL(*mTxnPtr, AddRule(_, std::string("forward"), _)).Times(2);
    EXPECT_CALL(*mTxnPtr, Commit(An<std::vector<FWRuleHandle>&>())).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_TRUE(mFirewall.AddInstance("test", params).IsNone());
}

TEST_F(FirewallTest, AddInstanceRejectsInputAccessWithoutPort)
{
    auto params = MakeParams("10.0.0.5", true);
    ASSERT_TRUE(params.mInput.PushBack(MakeInput("", "")).IsNone());

    auto tx = NewMockTx();
    EXPECT_CALL(mBackend, NewTxn()).WillOnce(Return(ByMove(std::move(tx))));
    EXPECT_CALL(*mTxnPtr, AddChain(_));
    EXPECT_CALL(*mTxnPtr, Commit()).Times(0);

    EXPECT_FALSE(mFirewall.AddInstance("test", params).IsNone());
}

TEST_F(FirewallTest, AddInstanceRejectsUnsupportedInputProtocol)
{
    auto params = MakeParams("10.0.0.5", true);
    ASSERT_TRUE(params.mInput.PushBack(MakeInput("80", "sctp")).IsNone());

    auto tx = NewMockTx();
    EXPECT_CALL(mBackend, NewTxn()).WillOnce(Return(ByMove(std::move(tx))));
    EXPECT_CALL(*mTxnPtr, AddChain(_));
    EXPECT_CALL(*mTxnPtr, Commit()).Times(0);

    EXPECT_FALSE(mFirewall.AddInstance("test", params).IsNone());
}

TEST_F(FirewallTest, AddInstanceRejectsEmptyInstanceIP)
{
    auto params = MakeParams("", true);

    EXPECT_CALL(mBackend, NewTxn()).Times(0);

    EXPECT_FALSE(mFirewall.AddInstance("test", params).IsNone());
}

TEST_F(FirewallTest, UpdateInstanceRejectsEmptyInstanceIP)
{
    auto params = MakeParams("", true);

    EXPECT_CALL(mBackend, ListChainRules(_, _, _)).Times(0);
    EXPECT_CALL(mBackend, NewTxn()).Times(0);

    EXPECT_FALSE(mFirewall.UpdateInstance("test", params).IsNone());
}

TEST_F(FirewallTest, AddInstanceRejectsOutputAccessWithoutDstIP)
{
    auto params = MakeParams("10.0.0.5", false);
    ASSERT_TRUE(params.mOutput.PushBack(MakeOutput("", "", "")).IsNone());

    auto tx = NewMockTx();
    EXPECT_CALL(mBackend, NewTxn()).WillOnce(Return(ByMove(std::move(tx))));
    EXPECT_CALL(*mTxnPtr, AddChain(_));
    EXPECT_CALL(*mTxnPtr, Commit()).Times(0);

    EXPECT_FALSE(mFirewall.AddInstance("test", params).IsNone());
}

TEST_F(FirewallTest, AddInstanceRejectsOutputAccessWithoutDstPort)
{
    auto params = MakeParams("10.0.0.5", false);
    ASSERT_TRUE(params.mOutput.PushBack(MakeOutput("8.8.8.8", "", "tcp")).IsNone());

    auto tx = NewMockTx();
    EXPECT_CALL(mBackend, NewTxn()).WillOnce(Return(ByMove(std::move(tx))));
    EXPECT_CALL(*mTxnPtr, AddChain(_));
    EXPECT_CALL(*mTxnPtr, Commit()).Times(0);

    EXPECT_FALSE(mFirewall.AddInstance("test", params).IsNone());
}

TEST_F(FirewallTest, AddInstanceRejectsUnsupportedOutputProtocol)
{
    auto params = MakeParams("10.0.0.5", false);
    ASSERT_TRUE(params.mOutput.PushBack(MakeOutput("8.8.8.8", "53", "sctp")).IsNone());

    auto tx = NewMockTx();
    EXPECT_CALL(mBackend, NewTxn()).WillOnce(Return(ByMove(std::move(tx))));
    EXPECT_CALL(*mTxnPtr, AddChain(_));
    EXPECT_CALL(*mTxnPtr, Commit()).Times(0);

    EXPECT_FALSE(mFirewall.AddInstance("test", params).IsNone());
}

TEST_F(FirewallTest, AddInstanceRejectsInvalidPort)
{
    auto params = MakeParams("10.0.0.5", true);
    ASSERT_TRUE(params.mInput.PushBack(MakeInput("abc", "tcp")).IsNone());

    auto tx = NewMockTx();
    EXPECT_CALL(mBackend, NewTxn()).WillOnce(Return(ByMove(std::move(tx))));
    EXPECT_CALL(*mTxnPtr, AddChain(_));
    EXPECT_CALL(*mTxnPtr, Commit()).Times(0);

    EXPECT_FALSE(mFirewall.AddInstance("test", params).IsNone());
}

TEST_F(FirewallTest, AddInstanceRejectsZeroPort)
{
    auto params = MakeParams("10.0.0.5", true);
    ASSERT_TRUE(params.mInput.PushBack(MakeInput("0", "tcp")).IsNone());

    auto tx = NewMockTx();
    EXPECT_CALL(mBackend, NewTxn()).WillOnce(Return(ByMove(std::move(tx))));
    EXPECT_CALL(*mTxnPtr, AddChain(_));
    EXPECT_CALL(*mTxnPtr, Commit()).Times(0);

    EXPECT_FALSE(mFirewall.AddInstance("test", params).IsNone());
}

/***********************************************************************************************************************
 * Port ranges
 **********************************************************************************************************************/

TEST_F(FirewallTest, AddInstanceExposedRangeProducesSingleRangedRule)
{
    auto params = MakeParams("10.0.0.5", true);

    ASSERT_TRUE(params.mInput.PushBack(MakeInput("7400:7650", "udp")).IsNone());

    auto tx = NewMockTx();

    InSequence seq;
    EXPECT_CALL(mBackend, NewTxn()).WillOnce(Return(ByMove(std::move(tx))));
    EXPECT_CALL(*mTxnPtr, AddChain(ChainNamed("instance_test")));
    EXPECT_CALL(*mTxnPtr,
        AddRule(_, std::string("instance_test"),
            InputRangeRule(std::string("10.0.0.5"), std::string("udp"), 7400, 7650, FWActionEnum::eAccept)));
    EXPECT_CALL(*mTxnPtr,
        AddRule(_, std::string("instance_test"), TerminalInRule(std::string("10.0.0.5"), FWActionEnum::eDrop)));
    EXPECT_CALL(*mTxnPtr,
        AddRule(_, std::string("instance_test"), TerminalOutRule(std::string("10.0.0.5"), FWActionEnum::eAccept)));
    EXPECT_CALL(*mTxnPtr, AddRule(_, std::string("forward"), _)).Times(2);
    EXPECT_CALL(*mTxnPtr, Commit(An<std::vector<FWRuleHandle>&>())).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_TRUE(mFirewall.AddInstance("test", params).IsNone());
}

TEST_F(FirewallTest, AddInstanceOutputRangeTranslated)
{
    auto params = MakeParams("10.0.0.5", true);

    ASSERT_TRUE(params.mOutput.PushBack(MakeOutput("10.0.1.7", "7400:7650", "udp")).IsNone());

    auto tx = NewMockTx();

    InSequence seq;
    EXPECT_CALL(mBackend, NewTxn()).WillOnce(Return(ByMove(std::move(tx))));
    EXPECT_CALL(*mTxnPtr, AddChain(_));
    EXPECT_CALL(*mTxnPtr,
        AddRule(_, std::string("instance_test"),
            OutputRangeRule(std::string("10.0.0.5"), std::string("10.0.1.7"), std::string("udp"), 7400, 7650,
                FWActionEnum::eAccept)));
    EXPECT_CALL(*mTxnPtr,
        AddRule(_, std::string("instance_test"), TerminalInRule(std::string("10.0.0.5"), FWActionEnum::eDrop)));
    EXPECT_CALL(*mTxnPtr,
        AddRule(_, std::string("instance_test"), TerminalOutRule(std::string("10.0.0.5"), FWActionEnum::eAccept)));
    EXPECT_CALL(*mTxnPtr, AddRule(_, std::string("forward"), _)).Times(2);
    EXPECT_CALL(*mTxnPtr, Commit(An<std::vector<FWRuleHandle>&>())).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_TRUE(mFirewall.AddInstance("test", params).IsNone());
}

TEST_F(FirewallTest, AddInstanceSinglePortLeavesRangeEndUnset)
{
    // A degenerate range must be emitted as a plain `dport <port>`, otherwise
    // ListChainRules() would parse back a rule that differs from the added one.
    auto params = MakeParams("10.0.0.5", true);

    ASSERT_TRUE(params.mInput.PushBack(MakeInput("8080:8080", "tcp")).IsNone());

    auto tx = NewMockTx();

    InSequence seq;
    EXPECT_CALL(mBackend, NewTxn()).WillOnce(Return(ByMove(std::move(tx))));
    EXPECT_CALL(*mTxnPtr, AddChain(_));
    EXPECT_CALL(*mTxnPtr,
        AddRule(_, std::string("instance_test"),
            InputRule(std::string("10.0.0.5"), std::string("tcp"), 8080, FWActionEnum::eAccept)));
    EXPECT_CALL(*mTxnPtr, AddRule(_, std::string("instance_test"), _)).Times(2);
    EXPECT_CALL(*mTxnPtr, AddRule(_, std::string("forward"), _)).Times(2);
    EXPECT_CALL(*mTxnPtr, Commit(An<std::vector<FWRuleHandle>&>())).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_TRUE(mFirewall.AddInstance("test", params).IsNone());
}

TEST_F(FirewallTest, AddInstanceRejectsInvalidPortRanges)
{
    for (const auto* port :
        {"0:10", "10:5", "1:70000", "abc", "7400:", ":7650", "7400::7650", "74 00:7650", "8080-8081"}) {
        auto params = MakeParams("10.0.0.5", true);
        ASSERT_TRUE(params.mInput.PushBack(MakeInput(port, "udp")).IsNone());

        auto tx = NewMockTx();
        EXPECT_CALL(mBackend, NewTxn()).WillOnce(Return(ByMove(std::move(tx))));
        EXPECT_CALL(*mTxnPtr, AddChain(_));
        EXPECT_CALL(*mTxnPtr, Commit()).Times(0);

        EXPECT_FALSE(mFirewall.AddInstance("test", params).IsNone()) << port;

        Mock::VerifyAndClearExpectations(&mBackend);
    }
}

/***********************************************************************************************************************
 * UpdateInstance
 **********************************************************************************************************************/

TEST_F(FirewallTest, UpdateInstanceFlushesRepopulatesAndRepointsJumps)
{
    auto params = MakeParams("10.0.0.9", true);
    ASSERT_TRUE(params.mInput.PushBack(MakeInput("9090", "tcp")).IsNone());

    std::vector<FWListedRule> forwardRules;
    forwardRules.push_back({{"10.0.0.5", "", "", 0, 0, "", FWActionEnum::eJump, "instance_test"}, FWRuleHandle {20}});
    forwardRules.push_back({{"", "10.0.0.5", "", 0, 0, "", FWActionEnum::eJump, "instance_test"}, FWRuleHandle {21}});

    auto tx = NewMockTx();

    InSequence seq;
    EXPECT_CALL(mBackend, ListChainRules(_, std::string("forward"), _))
        .WillOnce(DoAll(SetArgReferee<2>(forwardRules), Return(ErrorEnum::eNone)));
    EXPECT_CALL(mBackend, NewTxn()).WillOnce(Return(ByMove(std::move(tx))));
    EXPECT_CALL(*mTxnPtr, FlushChain(_, std::string("instance_test")));
    EXPECT_CALL(*mTxnPtr,
        AddRule(_, std::string("instance_test"),
            InputRule(std::string("10.0.0.9"), std::string("tcp"), 9090, FWActionEnum::eAccept)));
    EXPECT_CALL(*mTxnPtr,
        AddRule(_, std::string("instance_test"), TerminalInRule(std::string("10.0.0.9"), FWActionEnum::eDrop)));
    EXPECT_CALL(*mTxnPtr,
        AddRule(_, std::string("instance_test"), TerminalOutRule(std::string("10.0.0.9"), FWActionEnum::eAccept)));
    EXPECT_CALL(*mTxnPtr, DeleteRuleByHandle(_, std::string("forward"), FWRuleHandle {20}));
    EXPECT_CALL(*mTxnPtr, DeleteRuleByHandle(_, std::string("forward"), FWRuleHandle {21}));
    EXPECT_CALL(*mTxnPtr,
        AddRule(_, std::string("forward"),
            AllOf(Field(&FWRule::mDstAddr, "10.0.0.9"), Field(&FWRule::mJumpTarget, "instance_test"))));
    EXPECT_CALL(*mTxnPtr,
        AddRule(_, std::string("forward"),
            AllOf(Field(&FWRule::mSrcAddr, "10.0.0.9"), Field(&FWRule::mJumpTarget, "instance_test"))));
    EXPECT_CALL(*mTxnPtr, Commit(An<std::vector<FWRuleHandle>&>())).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_TRUE(mFirewall.UpdateInstance("test", params).IsNone());
}

/***********************************************************************************************************************
 * RemoveInstance
 **********************************************************************************************************************/

TEST_F(FirewallTest, RemoveInstanceDeletesJumpsAndChain)
{
    std::vector<FWListedRule> forwardRules;
    forwardRules.push_back({{"10.0.0.5", "", "", 0, 0, "", FWActionEnum::eJump, "instance_test"}, FWRuleHandle {11}});
    forwardRules.push_back({{"", "10.0.0.5", "", 0, 0, "", FWActionEnum::eJump, "instance_test"}, FWRuleHandle {12}});
    forwardRules.push_back({{"", "10.0.0.7", "", 0, 0, "", FWActionEnum::eJump, "instance_other"}, FWRuleHandle {13}});

    auto tx = NewMockTx();

    InSequence seq;
    EXPECT_CALL(mBackend, ListChainRules(_, std::string("forward"), _))
        .WillOnce(DoAll(SetArgReferee<2>(forwardRules), Return(ErrorEnum::eNone)));
    EXPECT_CALL(mBackend, NewTxn()).WillOnce(Return(ByMove(std::move(tx))));
    EXPECT_CALL(*mTxnPtr, DeleteRuleByHandle(_, std::string("forward"), FWRuleHandle {11}));
    EXPECT_CALL(*mTxnPtr, DeleteRuleByHandle(_, std::string("forward"), FWRuleHandle {12}));
    EXPECT_CALL(*mTxnPtr, FlushChain(_, std::string("instance_test")));
    EXPECT_CALL(*mTxnPtr, DeleteChain(_, std::string("instance_test")));
    EXPECT_CALL(*mTxnPtr, Commit()).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_TRUE(mFirewall.RemoveInstance("test").IsNone());
}

TEST_F(FirewallTest, RemoveInstanceNoMatchIsNoOp)
{
    std::vector<FWListedRule> forwardRules;
    forwardRules.push_back({{"", "10.0.0.7", "", 0, 0, "", FWActionEnum::eJump, "instance_other"}, FWRuleHandle {13}});

    EXPECT_CALL(mBackend, ListChainRules(_, std::string("forward"), _))
        .WillOnce(DoAll(SetArgReferee<2>(forwardRules), Return(ErrorEnum::eNone)));

    EXPECT_CALL(mBackend, NewTxn()).Times(0);

    EXPECT_TRUE(mFirewall.RemoveInstance("test").IsNone());
}

/***********************************************************************************************************************
 * Batch
 **********************************************************************************************************************/

TEST_F(FirewallTest, BatchStagesInstancesIntoSingleCommit)
{
    auto tx = NewMockTx();

    InSequence seq;
    EXPECT_CALL(mBackend, NewTxn()).WillOnce(Return(ByMove(std::move(tx))));
    EXPECT_CALL(*mTxnPtr, AddChain(ChainNamed("instance_inst1")));
    EXPECT_CALL(*mTxnPtr, AddRule(_, std::string("instance_inst1"), _)).Times(2);
    EXPECT_CALL(*mTxnPtr, AddRule(_, std::string("forward"), Field(&FWRule::mJumpTarget, "instance_inst1"))).Times(2);
    EXPECT_CALL(*mTxnPtr, AddChain(ChainNamed("instance_inst2")));
    EXPECT_CALL(*mTxnPtr, AddRule(_, std::string("instance_inst2"), _)).Times(2);
    EXPECT_CALL(*mTxnPtr, AddRule(_, std::string("forward"), Field(&FWRule::mJumpTarget, "instance_inst2"))).Times(2);
    EXPECT_CALL(*mTxnPtr, Commit(An<std::vector<FWListedRule>&>())).WillOnce(Return(ErrorEnum::eNone));

    ASSERT_TRUE(mFirewall.BeginBatch().IsNone());
    ASSERT_TRUE(mFirewall.AddInstance("inst1", MakeParams("10.0.0.5", true)).IsNone());
    ASSERT_TRUE(mFirewall.AddInstance("inst2", MakeParams("10.0.0.6", true)).IsNone());

    EXPECT_TRUE(mFirewall.FlushBatch().IsNone());
}

TEST_F(FirewallTest, BatchStagesRemoveInstanceDeletes)
{
    std::vector<FWListedRule> forwardRules;
    forwardRules.push_back({{"10.0.0.5", "", "", 0, 0, "", FWActionEnum::eJump, "instance_test"}, FWRuleHandle {11}});
    forwardRules.push_back({{"", "10.0.0.5", "", 0, 0, "", FWActionEnum::eJump, "instance_test"}, FWRuleHandle {12}});

    auto tx = NewMockTx();

    InSequence seq;
    EXPECT_CALL(mBackend, NewTxn()).WillOnce(Return(ByMove(std::move(tx))));
    EXPECT_CALL(mBackend, ListChainRules(_, std::string("forward"), _))
        .WillOnce(DoAll(SetArgReferee<2>(forwardRules), Return(ErrorEnum::eNone)));
    EXPECT_CALL(*mTxnPtr, DeleteRuleByHandle(_, std::string("forward"), FWRuleHandle {11}));
    EXPECT_CALL(*mTxnPtr, DeleteRuleByHandle(_, std::string("forward"), FWRuleHandle {12}));
    EXPECT_CALL(*mTxnPtr, FlushChain(_, std::string("instance_test")));
    EXPECT_CALL(*mTxnPtr, DeleteChain(_, std::string("instance_test")));
    EXPECT_CALL(*mTxnPtr, Commit(An<std::vector<FWListedRule>&>())).WillOnce(Return(ErrorEnum::eNone));

    ASSERT_TRUE(mFirewall.BeginBatch().IsNone());
    ASSERT_TRUE(mFirewall.RemoveInstance("test").IsNone());

    EXPECT_TRUE(mFirewall.FlushBatch().IsNone());
}

TEST_F(FirewallTest, AddInstanceCommitsImmediatelyAfterFlushBatch)
{
    auto  batchTx  = NewMockTx();
    auto* batchPtr = mTxnPtr;

    auto  directTx  = NewMockTx();
    auto* directPtr = mTxnPtr;

    EXPECT_CALL(mBackend, NewTxn())
        .WillOnce(Return(ByMove(std::move(batchTx))))
        .WillOnce(Return(ByMove(std::move(directTx))));

    EXPECT_CALL(*batchPtr, AddChain(_));
    EXPECT_CALL(*batchPtr, AddRule(_, _, _)).Times(4).WillRepeatedly(Return(ErrorEnum::eNone));
    EXPECT_CALL(*batchPtr, Commit(An<std::vector<FWListedRule>&>())).WillOnce(Return(ErrorEnum::eNone));

    ASSERT_TRUE(mFirewall.BeginBatch().IsNone());
    ASSERT_TRUE(mFirewall.AddInstance("inst1", MakeParams("10.0.0.5", true)).IsNone());
    ASSERT_TRUE(mFirewall.FlushBatch().IsNone());

    // Batch is over: the next instance gets its own transaction and commits now.
    EXPECT_CALL(*directPtr, AddChain(ChainNamed("instance_inst2")));
    EXPECT_CALL(*directPtr, AddRule(_, _, _)).Times(4).WillRepeatedly(Return(ErrorEnum::eNone));
    EXPECT_CALL(*directPtr, Commit(An<std::vector<FWRuleHandle>&>())).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_TRUE(mFirewall.AddInstance("inst2", MakeParams("10.0.0.6", true)).IsNone());
}

TEST_F(FirewallTest, RevertDeletesFlushedHandlesAndBatchChains)
{
    auto  batchTx  = NewMockTx();
    auto* batchPtr = mTxnPtr;

    auto  revertTx  = NewMockTx();
    auto* revertPtr = mTxnPtr;

    EXPECT_CALL(mBackend, NewTxn())
        .WillOnce(Return(ByMove(std::move(batchTx))))
        .WillOnce(Return(ByMove(std::move(revertTx))));

    EXPECT_CALL(*batchPtr, AddChain(_));
    EXPECT_CALL(*batchPtr, AddRule(_, _, _)).Times(4).WillRepeatedly(Return(ErrorEnum::eNone));
    EXPECT_CALL(*batchPtr, Commit(An<std::vector<FWListedRule>&>())).WillOnce([](std::vector<FWListedRule>& added) {
        added = {
            {{}, FWRuleHandle {100}},
            {{}, FWRuleHandle {101}},
            {{"10.0.0.5", "", "", 0, 0, "", FWActionEnum::eJump, "instance_inst1"}, FWRuleHandle {102}},
            {{"", "10.0.0.5", "", 0, 0, "", FWActionEnum::eJump, "instance_inst1"}, FWRuleHandle {103}},
        };

        return Error(ErrorEnum::eNone);
    });

    ASSERT_TRUE(mFirewall.BeginBatch().IsNone());
    ASSERT_TRUE(mFirewall.AddInstance("inst1", MakeParams("10.0.0.5", true)).IsNone());
    ASSERT_TRUE(mFirewall.FlushBatch().IsNone());

    std::vector<FWListedRule> forwardRules;
    forwardRules.push_back({{"10.0.0.5", "", "", 0, 0, "", FWActionEnum::eJump, "instance_inst1"}, FWRuleHandle {102}});
    forwardRules.push_back({{"", "10.0.0.5", "", 0, 0, "", FWActionEnum::eJump, "instance_inst1"}, FWRuleHandle {103}});
    forwardRules.push_back({{"", "10.0.0.9", "", 0, 0, "", FWActionEnum::eJump, "instance_other"}, FWRuleHandle {7}});

    InSequence seq;
    EXPECT_CALL(mBackend, ListChainRules(_, std::string("forward"), _))
        .WillOnce(DoAll(SetArgReferee<2>(forwardRules), Return(ErrorEnum::eNone)));
    EXPECT_CALL(*revertPtr, DeleteRuleByHandle(_, std::string("forward"), FWRuleHandle {102}));
    EXPECT_CALL(*revertPtr, DeleteRuleByHandle(_, std::string("forward"), FWRuleHandle {103}));
    EXPECT_CALL(*revertPtr, FlushChain(_, std::string("instance_inst1")));
    EXPECT_CALL(*revertPtr, DeleteChain(_, std::string("instance_inst1")));
    EXPECT_CALL(*revertPtr, Commit()).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_TRUE(mFirewall.Revert().IsNone());
}

TEST_F(FirewallTest, RevertAfterFailedFlushIsNoOp)
{
    auto tx = NewMockTx();

    EXPECT_CALL(mBackend, NewTxn()).WillOnce(Return(ByMove(std::move(tx))));
    EXPECT_CALL(*mTxnPtr, AddChain(_));
    EXPECT_CALL(*mTxnPtr, AddRule(_, _, _)).Times(4).WillRepeatedly(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mTxnPtr, Commit(An<std::vector<FWListedRule>&>())).WillOnce(Return(Error(ErrorEnum::eFailed)));

    ASSERT_TRUE(mFirewall.BeginBatch().IsNone());
    ASSERT_TRUE(mFirewall.AddInstance("inst1", MakeParams("10.0.0.5", true)).IsNone());

    EXPECT_FALSE(mFirewall.FlushBatch().IsNone());

    // The batch was atomic: nothing was applied, so there is nothing to undo.
    EXPECT_TRUE(mFirewall.Revert().IsNone());
}

TEST_F(FirewallTest, FlushBatchAndRevertWithoutBeginAreNoOp)
{
    EXPECT_CALL(mBackend, NewTxn()).Times(0);
    EXPECT_CALL(mBackend, ListChainRules(_, _, _)).Times(0);

    EXPECT_TRUE(mFirewall.FlushBatch().IsNone());
    EXPECT_TRUE(mFirewall.Revert().IsNone());
}

/***********************************************************************************************************************
 * Masquerade
 **********************************************************************************************************************/

TEST_F(FirewallTest, AddMasqueradeAddsRule)
{
    auto tx = NewMockTx();

    InSequence seq;
    EXPECT_CALL(mBackend, NewTxn()).WillOnce(Return(ByMove(std::move(tx))));
    EXPECT_CALL(*mTxnPtr,
        AddRule(_, std::string("postrouting"), MasqueradeRule(std::string("10.0.0.0/24"), std::string("br-test"))));
    EXPECT_CALL(*mTxnPtr, Commit()).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_TRUE(mFirewall.AddMasquerade("10.0.0.0/24", "br-test").IsNone());
}

TEST_F(FirewallTest, AddMasqueradeIsIdempotent)
{
    auto tx = NewMockTx();

    InSequence seq;
    EXPECT_CALL(mBackend, NewTxn()).WillOnce(Return(ByMove(std::move(tx))));
    EXPECT_CALL(*mTxnPtr, AddRule(_, _, _));
    EXPECT_CALL(*mTxnPtr, Commit()).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_TRUE(mFirewall.AddMasquerade("10.0.0.0/24", "br-test").IsNone());

    EXPECT_TRUE(mFirewall.AddMasquerade("10.0.0.0/24", "br-test").IsNone());
}

TEST_F(FirewallTest, RemoveMasqueradeFindsHandleAndDeletes)
{
    std::vector<FWListedRule> postRules;
    postRules.push_back({{"10.0.0.0/24", "", "", 0, 0, "br-test", FWActionEnum::eMasquerade, ""}, FWRuleHandle {7}});

    auto tx = NewMockTx();

    InSequence seq;
    EXPECT_CALL(mBackend, ListChainRules(_, std::string("postrouting"), _))
        .WillOnce(DoAll(SetArgReferee<2>(postRules), Return(ErrorEnum::eNone)));
    EXPECT_CALL(mBackend, NewTxn()).WillOnce(Return(ByMove(std::move(tx))));
    EXPECT_CALL(*mTxnPtr, DeleteRuleByHandle(_, std::string("postrouting"), FWRuleHandle {7}));
    EXPECT_CALL(*mTxnPtr, Commit()).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_TRUE(mFirewall.RemoveMasquerade("10.0.0.0/24", "br-test").IsNone());
}

TEST_F(FirewallTest, RemoveMasqueradeNotFoundIsNoOp)
{
    std::vector<FWListedRule> postRules;

    EXPECT_CALL(mBackend, ListChainRules(_, std::string("postrouting"), _))
        .WillOnce(DoAll(SetArgReferee<2>(postRules), Return(ErrorEnum::eNone)));

    EXPECT_CALL(mBackend, NewTxn()).Times(0);

    EXPECT_TRUE(mFirewall.RemoveMasquerade("10.0.0.0/24", "br-test").IsNone());
}
