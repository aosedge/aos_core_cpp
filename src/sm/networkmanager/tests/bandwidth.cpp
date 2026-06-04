/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>
#include <core/sm/networkmanager/tests/mocks/interfacefactorymock.hpp>
#include <core/sm/networkmanager/tests/mocks/interfacemanagermock.hpp>

#include <common/tests/mocks/tcbackendmock.hpp>
#include <sm/networkmanager/bandwidth.hpp>

using namespace aos;
using namespace aos::common::network;
using namespace aos::sm::networkmanager;
using namespace testing;

namespace {

constexpr auto     cHostIfName    = "vethabcdef01";
constexpr auto     cIFBName       = "ifb-12194584";
constexpr uint64_t cIngressRate   = 1000000;
constexpr uint64_t cEgressRate    = 2000000;
constexpr uint64_t cBurst         = 12800;
constexpr uint64_t cExpectedLimit = cBurst * 4;

// BandwidthParams rates are bits/sec; Bandwidth converts to bytes/sec for tc.
constexpr uint64_t cIngressRateBytes = cIngressRate / 8;
constexpr uint64_t cEgressRateBytes  = cEgressRate / 8;

MATCHER_P3(TBFEq, rate, burst, limit, "")
{
    return arg.mRate == rate && arg.mBurst == burst && arg.mLimit == limit;
}

} // namespace

class BandwidthTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        aos::tests::utils::InitLog();

        ASSERT_TRUE(mBandwidth.Init(mTC, mIfFactory, mIfMgr).IsNone());
    }

    StrictMock<MockTCBackend>        mTC;
    StrictMock<InterfaceFactoryMock> mIfFactory;
    StrictMock<InterfaceManagerMock> mIfMgr;
    Bandwidth                        mBandwidth;
};

/***********************************************************************************************************************
 * Apply
 **********************************************************************************************************************/

TEST_F(BandwidthTest, ApplyNoOpWhenBothRatesZero)
{
    BandwidthParams params {};

    // StrictMock asserts there are zero backend interactions.
    EXPECT_TRUE(mBandwidth.Apply(cHostIfName, params).IsNone());
}

TEST_F(BandwidthTest, ApplyIngressOnlyInstallsRootTBF)
{
    BandwidthParams params {};
    params.mIngressRate  = cIngressRate;
    params.mIngressBurst = cBurst;

    EXPECT_CALL(mTC, AddRootTBFQDisc(String(cHostIfName), TBFEq(cIngressRateBytes, cBurst, cExpectedLimit)))
        .WillOnce(Return(ErrorEnum::eNone));

    EXPECT_TRUE(mBandwidth.Apply(cHostIfName, params).IsNone());
}

TEST_F(BandwidthTest, ApplyEgressOnlyInstallsIFBChain)
{
    BandwidthParams params {};
    params.mEgressRate  = cEgressRate;
    params.mEgressBurst = cBurst;

    InSequence seq;
    EXPECT_CALL(mIfFactory, CreateLink(String(cIFBName), String("ifb"))).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mIfMgr, SetupLink(String(cIFBName), _)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mTC, AddIngressQDisc(String(cHostIfName))).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mTC, AddIngressMirredFilter(String(cHostIfName), String(cIFBName))).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mTC, AddRootTBFQDisc(String(cIFBName), TBFEq(cEgressRateBytes, cBurst, cExpectedLimit)))
        .WillOnce(Return(ErrorEnum::eNone));

    EXPECT_TRUE(mBandwidth.Apply(cHostIfName, params).IsNone());
}

TEST_F(BandwidthTest, ApplyBothDirectionsInstallsBoth)
{
    BandwidthParams params {};
    params.mIngressRate  = cIngressRate;
    params.mIngressBurst = cBurst;
    params.mEgressRate   = cEgressRate;
    params.mEgressBurst  = cBurst;

    InSequence seq;
    EXPECT_CALL(mTC, AddRootTBFQDisc(String(cHostIfName), TBFEq(cIngressRateBytes, cBurst, cExpectedLimit)))
        .WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mIfFactory, CreateLink(String(cIFBName), String("ifb"))).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mIfMgr, SetupLink(String(cIFBName), _)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mTC, AddIngressQDisc(String(cHostIfName))).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mTC, AddIngressMirredFilter(String(cHostIfName), String(cIFBName))).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mTC, AddRootTBFQDisc(String(cIFBName), TBFEq(cEgressRateBytes, cBurst, cExpectedLimit)))
        .WillOnce(Return(ErrorEnum::eNone));

    EXPECT_TRUE(mBandwidth.Apply(cHostIfName, params).IsNone());
}

TEST_F(BandwidthTest, ApplyRollsBackOnIFBCreateFailure)
{
    BandwidthParams params {};
    params.mIngressRate  = cIngressRate;
    params.mIngressBurst = cBurst;
    params.mEgressRate   = cEgressRate;
    params.mEgressBurst  = cBurst;

    InSequence seq;
    EXPECT_CALL(mTC, AddRootTBFQDisc(String(cHostIfName), _)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mIfFactory, CreateLink(String(cIFBName), String("ifb"))).WillOnce(Return(Error(ErrorEnum::eFailed)));
    EXPECT_CALL(mTC, DelRootTBFQDisc(String(cHostIfName))).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_FALSE(mBandwidth.Apply(cHostIfName, params).IsNone());
}

TEST_F(BandwidthTest, ApplyRollsBackOnIngressQDiscFailure)
{
    BandwidthParams params {};
    params.mEgressRate  = cEgressRate;
    params.mEgressBurst = cBurst;

    InSequence seq;
    EXPECT_CALL(mIfFactory, CreateLink(String(cIFBName), String("ifb"))).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mIfMgr, SetupLink(String(cIFBName), _)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mTC, AddIngressQDisc(String(cHostIfName))).WillOnce(Return(Error(ErrorEnum::eFailed)));
    EXPECT_CALL(mIfMgr, DeleteLink(String(cIFBName))).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_FALSE(mBandwidth.Apply(cHostIfName, params).IsNone());
}

TEST_F(BandwidthTest, ApplyRollsBackOnTBFFailure)
{
    BandwidthParams params {};
    params.mEgressRate  = cEgressRate;
    params.mEgressBurst = cBurst;

    InSequence seq;
    EXPECT_CALL(mIfFactory, CreateLink(String(cIFBName), String("ifb"))).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mIfMgr, SetupLink(String(cIFBName), _)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mTC, AddIngressQDisc(String(cHostIfName))).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mTC, AddIngressMirredFilter(String(cHostIfName), String(cIFBName))).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mTC, AddRootTBFQDisc(String(cIFBName), _)).WillOnce(Return(Error(ErrorEnum::eFailed)));
    EXPECT_CALL(mTC, DelIngressQDisc(String(cHostIfName))).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mIfMgr, DeleteLink(String(cIFBName))).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_FALSE(mBandwidth.Apply(cHostIfName, params).IsNone());
}

/***********************************************************************************************************************
 * Clear
 **********************************************************************************************************************/

TEST_F(BandwidthTest, ClearRemovesEverything)
{
    EXPECT_CALL(mTC, DelRootTBFQDisc(String(cHostIfName))).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mTC, DelIngressQDisc(String(cHostIfName))).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mIfMgr, DeleteLink(String(cIFBName))).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_TRUE(mBandwidth.Clear(cHostIfName).IsNone());
}

TEST_F(BandwidthTest, ClearTreatsMissingIFBAsSuccess)
{
    EXPECT_CALL(mTC, DelRootTBFQDisc(String(cHostIfName))).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mTC, DelIngressQDisc(String(cHostIfName))).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mIfMgr, DeleteLink(String(cIFBName))).WillOnce(Return(Error(ErrorEnum::eNotFound)));

    EXPECT_TRUE(mBandwidth.Clear(cHostIfName).IsNone());
}

TEST_F(BandwidthTest, ClearRunsEveryStepOnFailure)
{
    EXPECT_CALL(mTC, DelRootTBFQDisc(String(cHostIfName))).WillOnce(Return(Error(ErrorEnum::eFailed)));
    EXPECT_CALL(mTC, DelIngressQDisc(String(cHostIfName))).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mIfMgr, DeleteLink(String(cIFBName))).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_FALSE(mBandwidth.Clear(cHostIfName).IsNone());
}

/***********************************************************************************************************************
 * Determinism
 **********************************************************************************************************************/

TEST_F(BandwidthTest, ApplyAndClearShareIFBName)
{
    BandwidthParams params {};
    params.mEgressRate  = cEgressRate;
    params.mEgressBurst = cBurst;

    StaticString<cInterfaceLen> capturedOnApply;

    {
        InSequence seq;
        EXPECT_CALL(mIfFactory, CreateLink(_, String("ifb")))
            .WillOnce([&capturedOnApply](const String& name, const String&) {
                capturedOnApply = name;
                return ErrorEnum::eNone;
            });
        EXPECT_CALL(mIfMgr, SetupLink(_, _)).WillOnce(Return(ErrorEnum::eNone));
        EXPECT_CALL(mTC, AddIngressQDisc(_)).WillOnce(Return(ErrorEnum::eNone));
        EXPECT_CALL(mTC, AddIngressMirredFilter(_, _)).WillOnce(Return(ErrorEnum::eNone));
        EXPECT_CALL(mTC, AddRootTBFQDisc(_, _)).WillOnce(Return(ErrorEnum::eNone));
    }

    ASSERT_TRUE(mBandwidth.Apply(cHostIfName, params).IsNone());

    StaticString<cInterfaceLen> capturedOnClear;

    EXPECT_CALL(mTC, DelRootTBFQDisc(_)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mTC, DelIngressQDisc(_)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mIfMgr, DeleteLink(_)).WillOnce([&capturedOnClear](const String& name) {
        capturedOnClear = name;
        return ErrorEnum::eNone;
    });

    ASSERT_TRUE(mBandwidth.Clear(cHostIfName).IsNone());

    EXPECT_EQ(capturedOnApply, capturedOnClear);
}
