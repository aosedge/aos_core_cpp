/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>

#include <sm/launcher/runtimes/filecopy/config.hpp>

using namespace testing;

namespace aos::sm::launcher {

class FileCopyConfigTest : public Test {
protected:
    static void SetUpTestSuite() { tests::utils::InitLog(); }

    void SetUp() override
    {
        mConfig.isComponent = true;
        mConfig.mPlugin     = "filecopy";
        mConfig.mType       = "mycomponent";
        mConfig.mWorkingDir = "/tmp";
        mConfig.mConfig     = Poco::makeShared<Poco::JSON::Object>();
    }

    RuntimeConfig  mConfig;
    FileCopyConfig mComponentConfig;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(FileCopyConfigTest, EmptyConfig)
{
    auto err = ParseConfig(mConfig, mComponentConfig);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(mComponentConfig.mTargetPath, "/var/aos/components/mycomponent");
    EXPECT_EQ(mComponentConfig.mRuntimeDir, "/tmp/runtimes/mycomponent");
}

TEST_F(FileCopyConfigTest, ExplicitConfig)
{
    mConfig.mConfig->set("targetPath", "/opt/components/mycomponent");
    mConfig.mConfig->set("runtimeDir", "/var/lib/aos/sm/mycomponent");

    auto err = ParseConfig(mConfig, mComponentConfig);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(mComponentConfig.mTargetPath, "/opt/components/mycomponent");
    EXPECT_EQ(mComponentConfig.mRuntimeDir, "/var/lib/aos/sm/mycomponent");
}

} // namespace aos::sm::launcher
