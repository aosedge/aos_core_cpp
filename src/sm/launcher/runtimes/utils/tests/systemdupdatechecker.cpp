/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>

#include <common/utils/exception.hpp>
#include <common/utils/time.hpp>

#include <sm/launcher/runtimes/utils/systemdupdatechecker.hpp>
#include <sm/tests/mocks/systemdconnmock.hpp>

using namespace testing;

namespace aos::sm::launcher::utils {

class SystemdUpdateCheckerTest : public Test {
protected:
    const std::vector<std::string> cUnits = {"unit1.service", "unit2.service", "unit3.service"};

    void SetUp() override { tests::utils::InitLog(); }

    SystemdUpdateChecker            mUpdateChecker;
    aos::sm::utils::SystemdConnMock mSystemdConn;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(SystemdUpdateCheckerTest, Check)
{
    auto err = mUpdateChecker.Init(cUnits, mSystemdConn);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    for (const auto& unit : cUnits) {
        EXPECT_CALL(mSystemdConn, GetUnitStatus(unit))
            .WillOnce(Return(aos::sm::utils::UnitStatus {unit, aos::sm::utils::UnitStateEnum::eActive, 0}));
    }

    err = mUpdateChecker.Check();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(SystemdUpdateCheckerTest, CheckUnitIsFailed)
{
    auto err = mUpdateChecker.Init(cUnits, mSystemdConn);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_CALL(mSystemdConn, GetUnitStatus(cUnits[0]))
        .WillOnce(Return(aos::sm::utils::UnitStatus {cUnits[0], aos::sm::utils::UnitStateEnum::eActive, 0}));
    EXPECT_CALL(mSystemdConn, GetUnitStatus(cUnits[1]))
        .WillOnce(Return(aos::sm::utils::UnitStatus {cUnits[1], aos::sm::utils::UnitStateEnum::eFailed, 1}));
    EXPECT_CALL(mSystemdConn, GetUnitStatus(cUnits[2]))
        .WillOnce(Return(aos::sm::utils::UnitStatus {cUnits[2], aos::sm::utils::UnitStateEnum::eActive, 0}));

    err = mUpdateChecker.Check();
    ASSERT_TRUE(err.Is(ErrorEnum::eFailed)) << tests::utils::ErrorToStr(err);
}

} // namespace aos::sm::launcher::utils
