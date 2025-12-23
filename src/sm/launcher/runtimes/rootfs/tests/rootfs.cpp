/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>

#include <sm/launcher/runtimes/rootfs/rootfs.hpp>

using namespace testing;

namespace aos::sm::launcher {

namespace {

/***********************************************************************************************************************
 * Consts
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

} // namespace

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class RootfsRuntimeTest : public Test {
protected:
    static void SetUpTestSuite() { tests::utils::InitLog(); }

    void SetUp() override { }

    void TearDown() override { }

    RootfsRuntime mRootfsRuntime;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(RootfsRuntimeTest, StartInstance)
{
}

} // namespace aos::sm::launcher
