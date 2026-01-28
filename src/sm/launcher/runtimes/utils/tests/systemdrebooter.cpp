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

#include <sm/launcher/runtimes/utils/systemdrebooter.hpp>
#include <sm/tests/mocks/systemdconnmock.hpp>

using namespace testing;

namespace aos::sm::launcher::utils {

class SystemdRebooterTest : public Test {
protected:
    void SetUp() override { tests::utils::InitLog(); }

    SystemdRebooter                 mRebooter;
    aos::sm::utils::SystemdConnMock mSystemdConn;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(SystemdRebooterTest, Reboot)
{
    auto err = mRebooter.Init(mSystemdConn);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_CALL(mSystemdConn, StartUnit("reboot.target", "replace-irreversibly", Time::cMinutes))
        .WillOnce(Return(ErrorEnum::eNone));

    err = mRebooter.Reboot();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(SystemdRebooterTest, RebootFails)
{
    auto err = mRebooter.Init(mSystemdConn);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_CALL(mSystemdConn, StartUnit).WillOnce(Return(ErrorEnum::eFailed));

    err = mRebooter.Reboot();
    ASSERT_TRUE(err.Is(ErrorEnum::eFailed)) << tests::utils::ErrorToStr(err);
}

} // namespace aos::sm::launcher::utils
