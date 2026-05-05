/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>
#include <core/sm/networkmanager/tests/mocks/firewallmock.hpp>
#include <core/sm/networkmanager/tests/mocks/interfacemanagermock.hpp>

#include <sm/networkmanager/bridgenetwork.hpp>

using namespace aos;
using namespace aos::sm::networkmanager;
using namespace testing;

/***********************************************************************************************************************
 * Fixtures / helpers
 **********************************************************************************************************************/

namespace {

BridgeParams MakeParams(bool hairpin = true, bool ipMasq = true)
{
    BridgeParams p;

    p.mBridgeIfName    = "br-test";
    p.mNetNSPath       = "/run/netns/test-instance";
    p.mContainerIfName = "eth0";
    p.mIPWithMask      = "10.0.0.5/24";
    p.mGateway         = "10.0.0.1";
    p.mSubnet          = "10.0.0.0/24";
    p.mHairpin         = hairpin;
    p.mIPMasq          = ipMasq;

    return p;
}

} // namespace

class BridgeNetworkTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        aos::tests::utils::InitLog();

        ASSERT_TRUE(mBridge.Init(mNetIf, mFirewall).IsNone());
    }

    StrictMock<InterfaceManagerMock> mNetIf;
    StrictMock<FirewallMock>         mFirewall;
    BridgeNetwork                    mBridge;

    const String mInstanceID {"test-instance-id"};
};

/***********************************************************************************************************************
 * Attach
 **********************************************************************************************************************/

TEST_F(BridgeNetworkTest, AttachHappyPath)
{
    const auto params = MakeParams();

    InSequence seq;

    EXPECT_CALL(mNetIf, CreateVeth(_, String("eth0"))).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mNetIf, SetMasterLink(_, String("br-test"))).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mNetIf, SetupLink(_)).WillOnce(Return(ErrorEnum::eNone)); // host
    EXPECT_CALL(mNetIf, SetupLink(String("eth0"))).WillOnce(Return(ErrorEnum::eNone)); // peer pre-move
    EXPECT_CALL(mNetIf, MoveLinkToNamespace(String("eth0"), String("/run/netns/test-instance")))
        .WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mNetIf, AddAddress(String("eth0"), String("10.0.0.5/24"), String("/run/netns/test-instance")))
        .WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mNetIf, AddRoute(String("0.0.0.0/0"), String("10.0.0.1"), String("/run/netns/test-instance")))
        .WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mNetIf, SetHairpin(_, true)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mFirewall, AddMasquerade(String("10.0.0.0/24"), String("br-test"))).WillOnce(Return(ErrorEnum::eNone));

    BridgeAttachResult result;
    EXPECT_TRUE(mBridge.Attach(mInstanceID, params, result).IsNone());

    EXPECT_FALSE(result.mHostIfName.IsEmpty());
    EXPECT_EQ(result.mContainerIfName, String("eth0"));
}

TEST_F(BridgeNetworkTest, AttachNoHairpin)
{
    const auto params = MakeParams(/*hairpin=*/false, /*ipMasq=*/true);

    EXPECT_CALL(mNetIf, CreateVeth(_, _)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mNetIf, SetMasterLink(_, _)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mNetIf, SetupLink(_)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mNetIf, SetupLink(String("eth0"))).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mNetIf, MoveLinkToNamespace(_, _)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mNetIf, AddAddress(_, _, _)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mNetIf, AddRoute(_, _, _)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mNetIf, SetHairpin(_, _)).Times(0);
    EXPECT_CALL(mFirewall, AddMasquerade(_, _)).WillOnce(Return(ErrorEnum::eNone));

    BridgeAttachResult result;
    EXPECT_TRUE(mBridge.Attach(mInstanceID, params, result).IsNone());
}

TEST_F(BridgeNetworkTest, AttachNoIPMasq)
{
    const auto params = MakeParams(/*hairpin=*/true, /*ipMasq=*/false);

    EXPECT_CALL(mNetIf, CreateVeth(_, _)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mNetIf, SetMasterLink(_, _)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mNetIf, SetupLink(_)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mNetIf, SetupLink(String("eth0"))).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mNetIf, MoveLinkToNamespace(_, _)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mNetIf, AddAddress(_, _, _)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mNetIf, AddRoute(_, _, _)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mNetIf, SetHairpin(_, true)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mFirewall, AddMasquerade(_, _)).Times(0);

    BridgeAttachResult result;
    EXPECT_TRUE(mBridge.Attach(mInstanceID, params, result).IsNone());
}

// Verifies that a veth is deleted on rollback when any step after CreateVeth fails.
TEST_F(BridgeNetworkTest, AttachRollbackOnSetMasterLinkFailure)
{
    const auto params = MakeParams();

    EXPECT_CALL(mNetIf, CreateVeth(_, _)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mNetIf, SetMasterLink(_, _)).WillOnce(Return(Error(ErrorEnum::eFailed)));
    EXPECT_CALL(mNetIf, DeleteLink(_)).WillOnce(Return(ErrorEnum::eNone));

    BridgeAttachResult result;
    EXPECT_FALSE(mBridge.Attach(mInstanceID, params, result).IsNone());
}

TEST_F(BridgeNetworkTest, AttachRollbackOnMoveLinkToNamespaceFailure)
{
    const auto params = MakeParams();

    EXPECT_CALL(mNetIf, CreateVeth(_, _)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mNetIf, SetMasterLink(_, _)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mNetIf, SetupLink(_)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mNetIf, SetupLink(String("eth0"))).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mNetIf, MoveLinkToNamespace(_, _)).WillOnce(Return(Error(ErrorEnum::eFailed)));
    EXPECT_CALL(mNetIf, DeleteLink(_)).WillOnce(Return(ErrorEnum::eNone));

    BridgeAttachResult result;
    EXPECT_FALSE(mBridge.Attach(mInstanceID, params, result).IsNone());
}

TEST_F(BridgeNetworkTest, AttachRollbackOnAddMasqueradeFailure)
{
    const auto params = MakeParams();

    EXPECT_CALL(mNetIf, CreateVeth(_, _)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mNetIf, SetMasterLink(_, _)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mNetIf, SetupLink(_)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mNetIf, SetupLink(String("eth0"))).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mNetIf, MoveLinkToNamespace(_, _)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mNetIf, AddAddress(_, _, _)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mNetIf, AddRoute(_, _, _)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mNetIf, SetHairpin(_, _)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mFirewall, AddMasquerade(_, _)).WillOnce(Return(Error(ErrorEnum::eFailed)));
    EXPECT_CALL(mNetIf, DeleteLink(_)).WillOnce(Return(ErrorEnum::eNone));

    BridgeAttachResult result;
    EXPECT_FALSE(mBridge.Attach(mInstanceID, params, result).IsNone());
}

/***********************************************************************************************************************
 * Detach
 **********************************************************************************************************************/

TEST_F(BridgeNetworkTest, DetachHappyPath)
{
    EXPECT_CALL(mFirewall, RemoveMasquerade(String("10.0.0.0/24"), String("br-test")))
        .WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mNetIf, DeleteLink(_)).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_TRUE(mBridge.Detach(mInstanceID, String("br-test"), String("10.0.0.0/24")).IsNone());
}

TEST_F(BridgeNetworkTest, DetachEmptySubnetSkipsMasquerade)
{
    EXPECT_CALL(mFirewall, RemoveMasquerade(_, _)).Times(0);
    EXPECT_CALL(mNetIf, DeleteLink(_)).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_TRUE(mBridge.Detach(mInstanceID, String("br-test"), String("")).IsNone());
}

TEST_F(BridgeNetworkTest, DetachIgnoresMasqueradeNotFound)
{
    EXPECT_CALL(mFirewall, RemoveMasquerade(_, _)).WillOnce(Return(Error(ErrorEnum::eNotFound)));
    EXPECT_CALL(mNetIf, DeleteLink(_)).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_TRUE(mBridge.Detach(mInstanceID, String("br-test"), String("10.0.0.0/24")).IsNone());
}
