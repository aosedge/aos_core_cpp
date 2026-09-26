/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>
#include <sm/networkmanager/firewall.hpp>

#include "stubs/firewallbackend.hpp"

using namespace aos;
using namespace aos::sm::nftables;
using namespace aos::sm::networkmanager;

namespace {

InstanceFirewallParams MakeParams(const char* ip, const char* subnet, bool allowPublic = true)
{
    InstanceFirewallParams params;

    params.mIP          = ip;
    params.mSubnet      = subnet;
    params.mAllowPublic = allowPublic;

    return params;
}

void Expose(InstanceFirewallParams& params, const char* port = "7410", const char* proto = "udp")
{
    ASSERT_TRUE(params.mInput.PushBack({port, proto}).IsNone());
}

void Allow(InstanceFirewallParams& params, const char* ip, const char* port = "7410", const char* proto = "udp")
{
    ASSERT_TRUE(params.mOutput.PushBack({ip, port, proto, params.mIP}).IsNone());
}

} // namespace

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class FirewallTest : public testing::Test {
protected:
    void SetUp() override
    {
        aos::tests::utils::InitLog();

        ASSERT_TRUE(mFirewall.Init(mBackend).IsNone());
        ASSERT_TRUE(mFirewall.Start().IsNone());
    }

    void AddPair(bool receiverFirst = false)
    {
        if (receiverFirst) {
            ASSERT_TRUE(mFirewall.AddInstance("b", mB).IsNone());
        }

        ASSERT_TRUE(mFirewall.AddInstance("a", mA).IsNone());

        if (!receiverFirst) {
            ASSERT_TRUE(mFirewall.AddInstance("b", mB).IsNone());
        }
    }

    aos::sm::networkmanager::tests::FirewallBackend mBackend;
    Firewall                                        mFirewall;
    InstanceFirewallParams                          mA = MakeParams("172.18.0.3", "172.18.0.0/16");
    InstanceFirewallParams                          mB = MakeParams("172.17.0.3", "172.17.0.0/16");
    aos::sm::networkmanager::tests::Packet          mPacket {"172.18.0.3", "172.17.0.3"};
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(FirewallTest, SkeletonKeepsDefaultDropAndOrdersBothChecksBeforeAccept)
{
    EXPECT_EQ(mBackend.mState.mPolicies.at("forward"), FWActionEnum::eDrop);
    const auto& rules = mBackend.mState.mChains.at("forward");

    ASSERT_EQ(rules.size(), 5U);
    EXPECT_EQ(rules[2].mRule.mJumpTarget, "egress");
    EXPECT_EQ(rules[3].mRule.mJumpTarget, "ingress");
    EXPECT_EQ(rules[4].mRule.mJumpTarget, "accepted");
    EXPECT_FALSE(mBackend.Accepts(mPacket));
}

TEST_F(FirewallTest, ExposedPortWithoutAllowConnectionIsDeniedInBothCreationOrders)
{
    Expose(mB);
    AddPair();

    EXPECT_FALSE(mBackend.Accepts(mPacket));

    ASSERT_TRUE(mFirewall.RemoveInstance("a").IsNone());
    ASSERT_TRUE(mFirewall.RemoveInstance("b").IsNone());
    AddPair(true);

    EXPECT_FALSE(mBackend.Accepts(mPacket));
}

TEST_F(FirewallTest, AllowConnectionAndExposedPortPermitBothCreationOrders)
{
    Expose(mB);
    Allow(mA, mB.mIP.CStr());
    AddPair();

    EXPECT_TRUE(mBackend.Accepts(mPacket));

    ASSERT_TRUE(mFirewall.UpdateInstance("a", mA).IsNone());

    EXPECT_TRUE(mBackend.Accepts(mPacket));

    ASSERT_TRUE(mFirewall.UpdateInstance("b", mB).IsNone());

    EXPECT_TRUE(mBackend.Accepts(mPacket));
}

TEST_F(FirewallTest, AllowConnectionDoesNotBypassReceiverExposedPorts)
{
    Allow(mA, mB.mIP.CStr());
    AddPair();

    EXPECT_FALSE(mBackend.Accepts(mPacket));

    Expose(mB);
    ASSERT_TRUE(mFirewall.UpdateInstance("b", mB).IsNone());

    EXPECT_TRUE(mBackend.Accepts(mPacket));
}

TEST_F(FirewallTest, SameNetworkIsUnrestrictedWithoutExposedPortsOrAllowConnections)
{
    mB              = MakeParams("172.18.0.4", "172.18.0.0/16", false);
    mA.mAllowPublic = false;
    AddPair();

    for (const auto* proto : {"udp", "tcp", "icmp"}) {
        EXPECT_TRUE(mBackend.Accepts({mA.mIP.CStr(), mB.mIP.CStr(), proto, 65000}));
        EXPECT_TRUE(mBackend.Accepts({mB.mIP.CStr(), mA.mIP.CStr(), proto, 65000}));
    }
}

TEST_F(FirewallTest, PublicAccessDoesNotAuthorizeAnyReservedAosNetwork)
{
    ASSERT_TRUE(mFirewall.AddInstance("a", mA).IsNone());

    for (int octet = 17; octet <= 31; ++octet) {
        if (octet == 18) {
            continue;
        }

        EXPECT_FALSE(mBackend.Accepts({mA.mIP.CStr(), "172." + std::to_string(octet) + ".0.9"}));
    }

    EXPECT_TRUE(mBackend.Accepts({mA.mIP.CStr(), "8.8.8.8", "udp", 53}));
    EXPECT_TRUE(mBackend.Accepts({mA.mIP.CStr(), "192.168.1.10", "tcp", 443}));
    EXPECT_TRUE(mBackend.Accepts({mA.mIP.CStr(), "10.0.0.20", "tcp", 443}));
}

TEST_F(FirewallTest, DenyPublicKeepsExplicitConnectionAndSameNetworkWorking)
{
    mA.mAllowPublic = false;
    Expose(mB);
    Allow(mA, mB.mIP.CStr());
    AddPair();

    EXPECT_TRUE(mBackend.Accepts(mPacket));
    EXPECT_FALSE(mBackend.Accepts({mA.mIP.CStr(), "8.8.8.8"}));
    EXPECT_TRUE(mBackend.Accepts({mA.mIP.CStr(), "172.18.0.99", "tcp", 12}));
}

TEST_F(FirewallTest, RemoteNodesCheckTheirOwnSourceAndDestination)
{
    aos::sm::networkmanager::tests::FirewallBackend remoteBackend;

    Firewall remote;

    ASSERT_TRUE(remote.Init(remoteBackend).IsNone());
    ASSERT_TRUE(remote.Start().IsNone());
    ASSERT_TRUE(mFirewall.AddInstance("a", mA).IsNone());
    Expose(mB);
    ASSERT_TRUE(remote.AddInstance("b", mB).IsNone());

    EXPECT_FALSE(mBackend.Accepts(mPacket));
    EXPECT_TRUE(remoteBackend.Accepts(mPacket));

    Allow(mA, mB.mIP.CStr());
    ASSERT_TRUE(mFirewall.UpdateInstance("a", mA).IsNone());

    EXPECT_TRUE(mBackend.Accepts(mPacket) && remoteBackend.Accepts(mPacket));

    mB.mInput.Clear();
    ASSERT_TRUE(remote.UpdateInstance("b", mB).IsNone());

    EXPECT_FALSE(mBackend.Accepts(mPacket) && remoteBackend.Accepts(mPacket));
}

TEST_F(FirewallTest, ReplyTrafficIsAllowedButNewReverseConnectionNeedsPermission)
{
    Expose(mB);
    Expose(mA);
    Allow(mA, mB.mIP.CStr());
    AddPair();

    EXPECT_TRUE(mBackend.Accepts(mPacket));
    EXPECT_FALSE(mBackend.Accepts({mB.mIP.CStr(), mA.mIP.CStr()}));
    EXPECT_TRUE(mBackend.Accepts({mB.mIP.CStr(), mA.mIP.CStr(), "udp", 56000, "established"}));
    EXPECT_FALSE(mBackend.Accepts({mA.mIP.CStr(), mB.mIP.CStr(), "udp", 7410, "invalid"}));
}

TEST_F(FirewallTest, PortRangesAndProtocolsRestrictBothDirections)
{
    Expose(mB, "7410:7449");
    Allow(mA, mB.mIP.CStr(), "7410:7449");
    AddPair();

    for (uint16_t port : {7410, 7425, 7449}) {
        EXPECT_TRUE(mBackend.Accepts({mA.mIP.CStr(), mB.mIP.CStr(), "udp", port}));
    }

    for (uint16_t port : {7409, 7450}) {
        EXPECT_FALSE(mBackend.Accepts({mA.mIP.CStr(), mB.mIP.CStr(), "udp", port}));
    }

    EXPECT_FALSE(mBackend.Accepts({mA.mIP.CStr(), mB.mIP.CStr(), "tcp", 7410}));
}

TEST_F(FirewallTest, MissingProtocolDefaultsToTcp)
{
    Expose(mB, "8080", "");
    Allow(mA, mB.mIP.CStr(), "8080", "");
    AddPair();

    EXPECT_TRUE(mBackend.Accepts({mA.mIP.CStr(), mB.mIP.CStr(), "tcp", 8080}));
    EXPECT_FALSE(mBackend.Accepts({mA.mIP.CStr(), mB.mIP.CStr(), "udp", 8080}));
}

TEST_F(FirewallTest, RemovingPermissionBlocksNewConnections)
{
    Expose(mB);
    Allow(mA, mB.mIP.CStr());
    AddPair();

    EXPECT_TRUE(mBackend.Accepts(mPacket));

    mA.mOutput.Clear();
    ASSERT_TRUE(mFirewall.UpdateInstance("a", mA).IsNone());

    EXPECT_FALSE(mBackend.Accepts(mPacket));
}

TEST_F(FirewallTest, AddressUpdateRemovesOldDispatchAndAcceptRules)
{
    Expose(mB);
    Allow(mA, mB.mIP.CStr());
    AddPair();
    mA.mIP = "172.18.0.7";
    ASSERT_TRUE(mFirewall.UpdateInstance("a", mA).IsNone());

    EXPECT_TRUE(mBackend.Accepts({mA.mIP.CStr(), mB.mIP.CStr()}));
    EXPECT_FALSE(mBackend.Accepts({"172.18.0.3", "8.8.8.8"}));

    for (const auto& entry : mBackend.mState.mChains.at("accepted")) {
        EXPECT_NE(entry.mRule.mSrcAddr, "172.18.0.3");
    }
}

TEST_F(FirewallTest, RemoveInstanceRemovesBothChainsAndPublicAccess)
{
    ASSERT_TRUE(mFirewall.AddInstance("a", mA).IsNone());
    ASSERT_TRUE(mFirewall.RemoveInstance("a").IsNone());

    EXPECT_EQ(mBackend.mState.mChains.count("instance_a"), 0U);
    EXPECT_EQ(mBackend.mState.mChains.count("instance_a_out"), 0U);
    EXPECT_FALSE(mBackend.Accepts({mA.mIP.CStr(), "8.8.8.8"}));
    const auto commits = mBackend.mCommits;
    ASSERT_TRUE(mFirewall.RemoveInstance("a").IsNone());

    EXPECT_EQ(mBackend.mCommits, commits);
}

TEST_F(FirewallTest, RestartAdoptsRulesWithoutRewritingAndSupportsRemoval)
{
    Expose(mB, "7410:7449");
    Allow(mA, mB.mIP.CStr(), "7410:7449");
    AddPair();

    Firewall restarted;

    ASSERT_TRUE(restarted.Init(mBackend).IsNone());
    const auto commits = mBackend.mCommits;

    ASSERT_TRUE(restarted.Start().IsNone());

    EXPECT_EQ(mBackend.mCommits, commits);
    EXPECT_TRUE(mBackend.Accepts(mPacket));
    EXPECT_TRUE(mBackend.Accepts({mA.mIP.CStr(), "8.8.8.8"}));
    EXPECT_FALSE(mBackend.Accepts({mB.mIP.CStr(), mA.mIP.CStr()}));

    ASSERT_TRUE(restarted.RemoveInstance("a").IsNone());

    EXPECT_FALSE(mBackend.Accepts({mA.mIP.CStr(), "8.8.8.8"}));
}

TEST_F(FirewallTest, StopKeepsDefaultDropAndRemovesInstanceAndNatRules)
{
    AddPair();
    ASSERT_TRUE(mFirewall.AddMasquerade("172.18.0.0/16", "eth0").IsNone());
    ASSERT_TRUE(mFirewall.Stop().IsNone());

    EXPECT_EQ(mBackend.mState.mPolicies.at("forward"), FWActionEnum::eDrop);
    EXPECT_TRUE(mBackend.mState.mChains.at("postrouting").empty());
    EXPECT_FALSE(mBackend.Accepts(mPacket));

    ASSERT_TRUE(mFirewall.Stop().IsNone());
}

TEST_F(FirewallTest, BatchStagesUntilSingleAtomicCommit)
{
    const auto commits = mBackend.mCommits;
    ASSERT_TRUE(mFirewall.BeginBatch().IsNone());
    Expose(mB);
    Allow(mA, mB.mIP.CStr());
    AddPair();

    EXPECT_FALSE(mBackend.Accepts(mPacket));
    EXPECT_EQ(mBackend.mCommits, commits);

    ASSERT_TRUE(mFirewall.FlushBatch().IsNone());

    EXPECT_EQ(mBackend.mCommits, commits + 1);
    EXPECT_TRUE(mBackend.Accepts(mPacket));

    ASSERT_TRUE(mFirewall.Revert().IsNone());

    EXPECT_FALSE(mBackend.Accepts(mPacket));
}

TEST_F(FirewallTest, BatchRemovalIsDeferredUntilFlush)
{
    Expose(mB);
    Allow(mA, mB.mIP.CStr());
    AddPair();

    const auto commits = mBackend.mCommits;
    ASSERT_TRUE(mFirewall.BeginBatch().IsNone());
    ASSERT_TRUE(mFirewall.RemoveInstance("b").IsNone());

    EXPECT_TRUE(mBackend.Accepts(mPacket));
    EXPECT_EQ(mBackend.mCommits, commits);

    ASSERT_TRUE(mFirewall.FlushBatch().IsNone());

    EXPECT_EQ(mBackend.mCommits, commits + 1);
    EXPECT_FALSE(mBackend.mState.mChains.count("instance_b"));
    EXPECT_FALSE(mBackend.mState.mChains.count("instance_b_out"));
}

TEST_F(FirewallTest, FailedUpdateOrBatchKeepsPreviouslyInstalledRules)
{
    Expose(mB);
    Allow(mA, mB.mIP.CStr());
    AddPair();
    mA.mOutput.Clear();
    mBackend.mFailCommit = true;

    EXPECT_FALSE(mFirewall.UpdateInstance("a", mA).IsNone());
    EXPECT_TRUE(mBackend.Accepts(mPacket));

    ASSERT_TRUE(mFirewall.BeginBatch().IsNone());
    ASSERT_TRUE(mFirewall.RemoveInstance("a").IsNone());

    EXPECT_FALSE(mFirewall.FlushBatch().IsNone());

    mBackend.mFailCommit = false;
    ASSERT_TRUE(mFirewall.Revert().IsNone());

    EXPECT_TRUE(mBackend.Accepts(mPacket));

    ASSERT_TRUE(mFirewall.UpdateInstance("a", mA).IsNone());

    EXPECT_FALSE(mBackend.Accepts(mPacket));
}

TEST_F(FirewallTest, AbortBatchDoesNotChangeInstalledRules)
{
    ASSERT_TRUE(mFirewall.BeginBatch().IsNone());
    ASSERT_TRUE(mFirewall.AddInstance("a", mA).IsNone());
    ASSERT_TRUE(mFirewall.AbortBatch().IsNone());
    ASSERT_TRUE(mFirewall.FlushBatch().IsNone());
    ASSERT_TRUE(mFirewall.Revert().IsNone());

    EXPECT_FALSE(mBackend.Accepts({mA.mIP.CStr(), "8.8.8.8"}));
}

TEST_F(FirewallTest, RemoveOrphansAfterRestartKeepsKnownInstanceAndMasquerade)
{
    AddPair();
    ASSERT_TRUE(mFirewall.AddMasquerade("172.18.0.0/16", "eth0").IsNone());
    ASSERT_TRUE(mFirewall.AddMasquerade("172.17.0.0/16", "eth0").IsNone());

    StaticArray<StaticString<cIDLen>, 1> known;

    known.PushBack("a");

    StaticArray<MasqueradeParams, 1> nat;

    nat.PushBack({"172.18.0.0/16", "eth0"});

    Firewall restarted;

    ASSERT_TRUE(restarted.Init(mBackend).IsNone());
    ASSERT_TRUE(restarted.Start().IsNone());
    ASSERT_TRUE(restarted.RemoveOrphans(known, nat).IsNone());

    EXPECT_EQ(mBackend.mState.mChains.count("instance_b"), 0U);
    EXPECT_EQ(mBackend.mState.mChains.count("instance_b_out"), 0U);
    EXPECT_TRUE(mBackend.Accepts({mA.mIP.CStr(), "8.8.8.8"}));
    EXPECT_FALSE(mBackend.Accepts({mB.mIP.CStr(), "8.8.8.8"}));
    ASSERT_EQ(mBackend.mState.mChains.at("postrouting").size(), 1U);
    const auto commits = mBackend.mCommits;

    ASSERT_TRUE(restarted.AddMasquerade("172.18.0.0/16", "eth0").IsNone());

    EXPECT_EQ(mBackend.mCommits, commits);
}

TEST_F(FirewallTest, MasqueradeUsesPositiveUplinkMatchAndCanBeRemoved)
{
    ASSERT_TRUE(mFirewall.AddMasquerade("172.18.0.0/16", "eth0").IsNone());
    const auto& rule = mBackend.mState.mChains.at("postrouting").front().mRule;

    EXPECT_EQ(rule.mOIFName, "eth0");
    EXPECT_FALSE(rule.mOIFNeg);

    ASSERT_TRUE(mFirewall.RemoveMasquerade("172.18.0.0/16", "eth0").IsNone());

    EXPECT_TRUE(mBackend.mState.mChains.at("postrouting").empty());

    ASSERT_TRUE(mFirewall.RemoveMasquerade("172.18.0.0/16", "eth0").IsNone());
}

TEST_F(FirewallTest, InvalidPortsAndProtocolsAreRejectedWithoutInstallingRules)
{
    for (const auto* port : {"", "0", "65536", "abc", "10:5", ":5", "5:", "1:2:3"}) {
        mA.mInput.Clear();
        Expose(mA, port);

        EXPECT_FALSE(mFirewall.AddInstance("a", mA).IsNone()) << port;
        EXPECT_EQ(mBackend.mState.mChains.count("instance_a"), 0U);
    }

    mA.mInput.Clear();
    Expose(mA, "80", "sctp");

    EXPECT_FALSE(mFirewall.AddInstance("a", mA).IsNone());

    mA.mInput.Clear();

    for (const auto* port : {"", "0", "65536", "10:5"}) {
        mA.mOutput.Clear();
        Allow(mA, mB.mIP.CStr(), port);

        EXPECT_FALSE(mFirewall.AddInstance("a", mA).IsNone());
    }

    mA.mOutput.Clear();
    Allow(mA, "");

    EXPECT_FALSE(mFirewall.AddInstance("a", mA).IsNone());

    mA.mOutput.Clear();
    Allow(mA, mB.mIP.CStr(), "80", "sctp");

    EXPECT_FALSE(mFirewall.AddInstance("a", mA).IsNone());
}

TEST_F(FirewallTest, EmptyIPIsRejectedForAddAndUpdate)
{
    mA.mIP.Clear();

    EXPECT_FALSE(mFirewall.AddInstance("a", mA).IsNone());
    EXPECT_FALSE(mFirewall.UpdateInstance("a", mA).IsNone());
}

TEST_F(FirewallTest, BackendFailuresArePropagatedWithoutChangingLiveRules)
{
    mBackend.mFailAdd = true;

    EXPECT_FALSE(mFirewall.AddInstance("a", mA).IsNone());

    mBackend.mFailAdd = false;
    ASSERT_TRUE(mFirewall.AddInstance("a", mA).IsNone());
    mBackend.mFailList = "ingress";

    EXPECT_FALSE(mFirewall.UpdateInstance("a", mA).IsNone());
    EXPECT_TRUE(mBackend.Accepts({mA.mIP.CStr(), "8.8.8.8"}));
}

TEST(FirewallLifecycleTest, StopBeforeStartIsIdempotent)
{
    aos::sm::networkmanager::tests::FirewallBackend backend;

    Firewall firewall;

    ASSERT_TRUE(firewall.Init(backend).IsNone());

    EXPECT_TRUE(firewall.Stop().IsNone());
    EXPECT_TRUE(firewall.Stop().IsNone());
}

TEST_F(FirewallTest, BatchCommitKeepsNewAndExistingDispatchHandlesCached)
{
    ASSERT_TRUE(mFirewall.AddInstance("existing", MakeParams("172.19.0.3", "172.19.0.0/16")).IsNone());
    const auto lists   = mBackend.mLists;
    const auto commits = mBackend.mCommits;
    ASSERT_TRUE(mFirewall.BeginBatch().IsNone());
    AddPair();
    ASSERT_TRUE(mFirewall.FlushBatch().IsNone());

    EXPECT_EQ(mBackend.mCommits, commits + 1);
    EXPECT_EQ(mBackend.mLists, lists);

    ASSERT_TRUE(mFirewall.RemoveInstance("a").IsNone());
    ASSERT_TRUE(mFirewall.RemoveInstance("b").IsNone());
    ASSERT_TRUE(mFirewall.RemoveInstance("existing").IsNone());

    EXPECT_EQ(mBackend.mLists, lists);
}

TEST_F(FirewallTest, BatchRevertListsEachDispatchChainOnceAndKeepsOtherInstances)
{
    ASSERT_TRUE(mFirewall.AddInstance("existing", MakeParams("172.19.0.3", "172.19.0.0/16")).IsNone());
    ASSERT_TRUE(mFirewall.BeginBatch().IsNone());
    AddPair();
    ASSERT_TRUE(mFirewall.FlushBatch().IsNone());
    const auto lists   = mBackend.mLists;
    const auto commits = mBackend.mCommits;
    ASSERT_TRUE(mFirewall.Revert().IsNone());

    EXPECT_EQ(mBackend.mCommits, commits + 1);
    EXPECT_EQ(mBackend.mLists, lists + 3);
    EXPECT_EQ(mBackend.mState.mChains.count("instance_a"), 0U);
    EXPECT_EQ(mBackend.mState.mChains.count("instance_a_out"), 0U);
    EXPECT_EQ(mBackend.mState.mChains.count("instance_b"), 0U);
    EXPECT_EQ(mBackend.mState.mChains.count("instance_b_out"), 0U);
    EXPECT_TRUE(mBackend.Accepts({"172.19.0.3", "8.8.8.8"}));

    ASSERT_TRUE(mFirewall.RemoveInstance("existing").IsNone());

    EXPECT_EQ(mBackend.mLists, lists + 3);
}
