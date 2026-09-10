/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <cstdlib>
#include <fcntl.h>
#include <sched.h>
#include <sys/socket.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <gtest/gtest.h>

#include <sm/networkmanager/firewall.hpp>
#include <sm/nftables/nftables.hpp>

using namespace aos;
using namespace aos::sm::networkmanager;

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

// Opt-in kernel tests. Run only in a disposable container with NET_ADMIN and SYS_ADMIN.
class FirewallKernelTest : public testing::Test {
protected:
    void SetUp() override
    {
        if (std::getenv("AOS_TEST_NFT_KERNEL") == nullptr) {
            GTEST_SKIP() << "Set AOS_TEST_NFT_KERNEL in an isolated privileged test container";
        }

        mOriginalNS = open("/proc/self/ns/net", O_RDONLY | O_CLOEXEC);

        ASSERT_GE(mOriginalNS, 0);
        ASSERT_EQ(unshare(CLONE_NEWNET), 0);

        mIsolated = true;

        ASSERT_EQ(
            std::system("set -e; ip link set lo up; "
                        "ip netns add aos-fw-a; ip netns add aos-fw-b; "
                        "ip link add a-router type veth peer name a-client; "
                        "ip link add b-router type veth peer name b-client; "
                        "ip link set a-client netns aos-fw-a; ip link set b-client netns aos-fw-b; "
                        "ip addr add 172.18.0.1/16 dev a-router; ip link set a-router up; "
                        "ip addr add 172.17.0.1/16 dev b-router; ip link set b-router up; "
                        "ip -n aos-fw-a addr add 172.18.0.3/16 dev a-client; ip -n aos-fw-a link set a-client up; "
                        "ip -n aos-fw-a link set lo up; ip -n aos-fw-a route add default via 172.18.0.1; "
                        "ip -n aos-fw-b addr add 172.17.0.3/16 dev b-client; ip -n aos-fw-b link set b-client up; "
                        "ip -n aos-fw-b link set lo up; ip -n aos-fw-b route add default via 172.17.0.1; "
                        "echo 1 > /proc/sys/net/ipv4/ip_forward"),
            0);
        ASSERT_TRUE(mFirewall.Init(mBackend).IsNone());

        const auto err = mFirewall.Start();

        ASSERT_TRUE(err.IsNone()) << err.Message();

        mA.mIP          = "172.18.0.3";
        mA.mSubnet      = "172.18.0.0/16";
        mA.mAllowPublic = true;
        mB.mIP          = "172.17.0.3";
        mB.mSubnet      = "172.17.0.0/16";
        mB.mAllowPublic = true;
        ASSERT_TRUE(mB.mInput.PushBack({"7410:7449", "udp"}).IsNone());
    }

    void TearDown() override
    {
        if (mIsolated) {
            EXPECT_EQ(std::system("ip netns del aos-fw-a; ip netns del aos-fw-b"), 0);
            EXPECT_EQ(setns(mOriginalNS, CLONE_NEWNET), 0);
        }

        if (mOriginalNS >= 0) {
            close(mOriginalNS);
        }
    }

    int OpenSocket(const char* path)
    {
        const int current = open("/proc/self/ns/net", O_RDONLY | O_CLOEXEC);
        const int target  = open(path, O_RDONLY | O_CLOEXEC);

        if (current < 0 || target < 0) {
            if (current >= 0) {
                close(current);
            }

            if (target >= 0) {
                close(target);
            }

            return -1;
        }

        const bool entered  = setns(target, CLONE_NEWNET) == 0;
        const int  fd       = entered ? socket(AF_INET, SOCK_DGRAM, 0) : -1;
        const int  restored = setns(current, CLONE_NEWNET);

        close(current);
        close(target);
        EXPECT_EQ(restored, 0);

        return fd;
    }

    void CheckDelivery(bool expected, uint16_t port = 7410)
    {
        const int sender   = OpenSocket("/run/netns/aos-fw-a");
        const int receiver = OpenSocket("/run/netns/aos-fw-b");

        ASSERT_GE(sender, 0);
        ASSERT_GE(receiver, 0);

        sockaddr_in address {};

        address.sin_family = AF_INET;
        address.sin_port   = htons(port);
        ASSERT_EQ(inet_pton(AF_INET, "172.17.0.3", &address.sin_addr), 1);
        ASSERT_EQ(bind(receiver, reinterpret_cast<sockaddr*>(&address), sizeof(address)), 0);

        timeval timeout {0, 250000};

        ASSERT_EQ(setsockopt(receiver, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)), 0);

        const char marker = 'a';

        EXPECT_EQ(sendto(sender, &marker, 1, 0, reinterpret_cast<sockaddr*>(&address), sizeof(address)), 1);

        char received {};

        EXPECT_EQ(recv(receiver, &received, 1, 0) == 1, expected);

        close(sender);
        close(receiver);
    }

    int                         mOriginalNS {-1};
    bool                        mIsolated {};
    aos::sm::nftables::NFTables mBackend;
    Firewall                    mFirewall;
    InstanceFirewallParams      mA;
    InstanceFirewallParams      mB;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(FirewallKernelTest, EnforcesBothDirectionsRegardlessOfOrderAndSurvivesRestart)
{
    ASSERT_TRUE(mFirewall.AddInstance("b", mB).IsNone());
    ASSERT_TRUE(mFirewall.AddInstance("a", mA).IsNone());

    CheckDelivery(false);

    ASSERT_TRUE(mA.mOutput.PushBack({mB.mIP, "7410:7449", "udp", mA.mIP}).IsNone());
    ASSERT_TRUE(mFirewall.UpdateInstance("a", mA).IsNone());

    CheckDelivery(true);
    CheckDelivery(true, 7449);
    CheckDelivery(false, 7450);

    Firewall restarted;

    ASSERT_TRUE(restarted.Init(mBackend).IsNone());

    const auto err = restarted.Start();

    ASSERT_TRUE(err.IsNone()) << err.Message();

    CheckDelivery(true);

    mB.mInput.Clear();
    ASSERT_TRUE(restarted.UpdateInstance("b", mB).IsNone());

    CheckDelivery(false);

    ASSERT_TRUE(mB.mInput.PushBack({"7410:7449", "udp"}).IsNone());
    ASSERT_TRUE(restarted.UpdateInstance("b", mB).IsNone());
    mA.mOutput.Clear();
    ASSERT_TRUE(restarted.UpdateInstance("a", mA).IsNone());

    CheckDelivery(false);

    ASSERT_TRUE(restarted.Stop().IsNone());

    CheckDelivery(false);
}

TEST_F(FirewallKernelTest, BatchFlushRevertAndAbortKeepTransactionalBehavior)
{
    ASSERT_TRUE(mA.mOutput.PushBack({mB.mIP, "7410:7449", "udp", mA.mIP}).IsNone());
    ASSERT_TRUE(mFirewall.BeginBatch().IsNone());
    ASSERT_TRUE(mFirewall.AddInstance("b", mB).IsNone());
    ASSERT_TRUE(mFirewall.AddInstance("a", mA).IsNone());

    CheckDelivery(false);

    ASSERT_TRUE(mFirewall.FlushBatch().IsNone());

    CheckDelivery(true);

    ASSERT_TRUE(mFirewall.Revert().IsNone());

    CheckDelivery(false);

    ASSERT_TRUE(mFirewall.BeginBatch().IsNone());
    ASSERT_TRUE(mFirewall.AddInstance("a", mA).IsNone());
    ASSERT_TRUE(mFirewall.AddInstance("b", mB).IsNone());
    ASSERT_TRUE(mFirewall.AbortBatch().IsNone());

    CheckDelivery(false);

    ASSERT_TRUE(mFirewall.BeginBatch().IsNone());
    ASSERT_TRUE(mFirewall.AddInstance("a", mA).IsNone());
    ASSERT_TRUE(mFirewall.AddInstance("b", mB).IsNone());
    ASSERT_TRUE(mFirewall.FlushBatch().IsNone());

    CheckDelivery(true);

    ASSERT_TRUE(mFirewall.BeginBatch().IsNone());
    ASSERT_TRUE(mFirewall.RemoveInstance("a").IsNone());

    CheckDelivery(true);

    ASSERT_TRUE(mFirewall.FlushBatch().IsNone());
    mB.mInput.Clear();
    ASSERT_TRUE(mFirewall.UpdateInstance("b", mB).IsNone());

    CheckDelivery(false);
}
