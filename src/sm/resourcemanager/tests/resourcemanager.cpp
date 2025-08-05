/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>

#include <sm/resourcemanager/resourcemanager.hpp>

using namespace testing;

namespace aos::sm::resourcemanager {

class ResourcemanagerTest : public Test {
public:
    void SetUp() override { tests::utils::InitLog(); }

    HostDeviceManager mHostDeviceManager;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(ResourcemanagerTest, CheckDevice)
{
    ASSERT_TRUE(mHostDeviceManager.Init().IsNone());

    EXPECT_TRUE(mHostDeviceManager.CheckDevice("/dev/null").IsNone());
}

TEST_F(ResourcemanagerTest, CheckDeviceReturnsNotFound)
{
    ASSERT_TRUE(mHostDeviceManager.Init().IsNone());

    EXPECT_TRUE(mHostDeviceManager.CheckDevice("not found test folder").Is(ErrorEnum::eNotFound));
}

TEST_F(ResourcemanagerTest, CheckGroup)
{
    ASSERT_TRUE(mHostDeviceManager.Init().IsNone());

    EXPECT_TRUE(mHostDeviceManager.CheckGroup("root").IsNone());
}

TEST_F(ResourcemanagerTest, CheckGroupReturnsNotFound)
{
    ASSERT_TRUE(mHostDeviceManager.Init().IsNone());

    EXPECT_TRUE(mHostDeviceManager.CheckGroup("not found test group").Is(ErrorEnum::eNotFound));
}

} // namespace aos::sm::resourcemanager
