/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>

#include <sm/launcher/runtimes/rootfs/config.hpp>

using namespace testing;

namespace aos::sm::launcher {

class RootfsRuntimeConfigTest : public Test {
protected:
    static void SetUpTestSuite() { tests::utils::InitLog(); }

    void SetUp() override
    {
        mConfig.isComponent = true;
        mConfig.mPlugin     = "rootfs";
        mConfig.mType       = "rootfs";
        mConfig.mWorkingDir = "/tmp";
        mConfig.mConfig     = Poco::makeShared<Poco::JSON::Object>();
    }

    RuntimeConfig mConfig;
    RootfsConfig  mRootfsConfig;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(RootfsRuntimeConfigTest, EmptyRootfsConfig)
{
    auto err = ParseConfig(mConfig, mRootfsConfig);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(mRootfsConfig.mWorkingDir, "/tmp/runtimes/rootfs");
    EXPECT_EQ(mRootfsConfig.mVersionFilePath, "/etc/aos/version");
    EXPECT_TRUE(mRootfsConfig.mHealthCheckServices.empty());
}

TEST_F(RootfsRuntimeConfigTest, RootfsConfig)
{
    mConfig.mConfig->set("workingDir", "/tmp/testdir");
    mConfig.mConfig->set("versionFilePath", "/tmp/version.txt");

    mConfig.mConfig->set("healthCheckServices", Poco::JSON::Array::Ptr(new Poco::JSON::Array()));
    mConfig.mConfig->getArray("healthCheckServices")->add("service1");
    mConfig.mConfig->getArray("healthCheckServices")->add("service2");

    auto err = ParseConfig(mConfig, mRootfsConfig);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(mRootfsConfig.mWorkingDir, "/tmp/testdir");
    EXPECT_EQ(mRootfsConfig.mVersionFilePath, "/tmp/version.txt");

    ASSERT_EQ(mRootfsConfig.mHealthCheckServices.size(), 2);
    EXPECT_EQ(mRootfsConfig.mHealthCheckServices[0], "service1");
    EXPECT_EQ(mRootfsConfig.mHealthCheckServices[1], "service2");
}

} // namespace aos::sm::launcher
