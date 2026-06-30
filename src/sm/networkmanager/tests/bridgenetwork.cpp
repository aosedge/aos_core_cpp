/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>
#include <core/sm/networkmanager/tests/mocks/interfacemanagermock.hpp>

#include <sm/networkmanager/bridgenetwork.hpp>

using namespace aos;
using namespace aos::sm::networkmanager;
using namespace testing;

/***********************************************************************************************************************
 * Fixtures / helpers
 **********************************************************************************************************************/

namespace {

BridgeParams MakeParams(bool hairpin = true)
{
    BridgeParams p;

    p.mBridgeIfName    = "br-test";
    p.mNetNSPath       = "/run/netns/test-instance";
    p.mContainerIfName = "eth0";
    p.mIPWithMask      = "10.0.0.5/24";
    p.mGateway         = "10.0.0.1";
    p.mHairpin         = hairpin;

    return p;
}

} // namespace

class BridgeNetworkTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        aos::tests::utils::InitLog();

        ASSERT_TRUE(mBridge.Init(mNetIf).IsNone());
    }

    StrictMock<InterfaceManagerMock> mNetIf;
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

    // veth pair created with the peer placed in the instance netns (named eth0),
    // host side up and enslaved to the bridge, in a single call.
    EXPECT_CALL(mNetIf, CreateVethToNamespace(_, String("eth0"), String("/run/netns/test-instance"), String("br-test")))
        .WillOnce(Return(ErrorEnum::eNone));
    // peer brought up, addressed and default-routed inside the netns, in a single call.
    EXPECT_CALL(mNetIf,
        ConfigureInstanceInterface(
            String("eth0"), String("10.0.0.5/24"), String("10.0.0.1"), String("/run/netns/test-instance")))
        .WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mNetIf, SetHairpin(_, true)).WillOnce(Return(ErrorEnum::eNone));

    BridgeAttachResult result;
    EXPECT_TRUE(mBridge.Attach(mInstanceID, params, result).IsNone());

    EXPECT_FALSE(result.mHostIfName.IsEmpty());
    EXPECT_EQ(result.mContainerIfName, String("eth0"));
}

TEST_F(BridgeNetworkTest, AttachNoHairpin)
{
    const auto params = MakeParams(/*hairpin=*/false);

    EXPECT_CALL(mNetIf, CreateVethToNamespace(_, _, _, _)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mNetIf, ConfigureInstanceInterface(_, _, _, _)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mNetIf, SetHairpin(_, _)).Times(0);

    BridgeAttachResult result;
    EXPECT_TRUE(mBridge.Attach(mInstanceID, params, result).IsNone());
}

// The veth is deleted on rollback when a step after CreateVethToNamespace fails.
TEST_F(BridgeNetworkTest, AttachRollbackOnConfigureInstanceInterfaceFailure)
{
    const auto params = MakeParams();

    EXPECT_CALL(mNetIf, CreateVethToNamespace(_, _, _, _)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mNetIf, ConfigureInstanceInterface(_, _, _, _)).WillOnce(Return(Error(ErrorEnum::eFailed)));
    EXPECT_CALL(mNetIf, DeleteLink(_)).WillOnce(Return(ErrorEnum::eNone));

    BridgeAttachResult result;
    EXPECT_FALSE(mBridge.Attach(mInstanceID, params, result).IsNone());
}

// No cleanup is attempted when the veth pair itself fails to be created.
TEST_F(BridgeNetworkTest, AttachNoCleanupOnCreateVethFailure)
{
    const auto params = MakeParams();

    EXPECT_CALL(mNetIf, CreateVethToNamespace(_, _, _, _)).WillOnce(Return(Error(ErrorEnum::eFailed)));
    EXPECT_CALL(mNetIf, DeleteLink(_)).Times(0);

    BridgeAttachResult result;
    EXPECT_FALSE(mBridge.Attach(mInstanceID, params, result).IsNone());
}

/***********************************************************************************************************************
 * Detach
 **********************************************************************************************************************/

TEST_F(BridgeNetworkTest, DetachHappyPath)
{
    EXPECT_CALL(mNetIf, DeleteLink(_)).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_TRUE(mBridge.Detach(mInstanceID, String("br-test")).IsNone());
}

TEST_F(BridgeNetworkTest, DetachFailsOnDeleteLinkError)
{
    EXPECT_CALL(mNetIf, DeleteLink(_)).WillOnce(Return(Error(ErrorEnum::eFailed)));

    EXPECT_FALSE(mBridge.Detach(mInstanceID, String("br-test")).IsNone());
}
