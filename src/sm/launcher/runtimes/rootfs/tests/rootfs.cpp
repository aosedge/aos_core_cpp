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

namespace aos::sm::launcher::rootfs {

class RootfsTest : public Test {
protected:
    void SetUp() override { tests::utils::InitLog(); }
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(RootfsTest, Reboot)
{
}

} // namespace aos::sm::launcher::rootfs
