/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>

#include <gtest/gtest.h>

#include <common/utils/time.hpp>
#include <common/utils/utils.hpp>

#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>

#include <sm/launcher/runtimes/boot/config.hpp>

using namespace testing;

namespace aos::sm::launcher {

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class BootRuntimeConfigTest : public Test {
protected:
    static void SetUpTestSuite() { tests::utils::InitLog(); }

    void SetUp() override
    {
        mRuntimeConfig.mPlugin     = "boot";
        mRuntimeConfig.mType       = "boot";
        mRuntimeConfig.mWorkingDir = std::filesystem::current_path().string();
        mRuntimeConfig.mConfig     = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);
    }

    RuntimeConfig mRuntimeConfig;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(BootRuntimeConfigTest, ParseEmptyConfig)
{
    BootConfig bootConfig;

    auto err = ParseConfig(mRuntimeConfig, bootConfig);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(bootConfig.mWorkingDir, std::filesystem::current_path() / "runtimes" / "boot");
    EXPECT_TRUE(bootConfig.mLoader.empty());
    EXPECT_EQ(bootConfig.mDetectMode, BootDetectModeEnum::eNone);
    EXPECT_TRUE(bootConfig.mPartitions.empty());
    EXPECT_TRUE(bootConfig.mHealthCheckServices.empty());
}

TEST_F(BootRuntimeConfigTest, ParseConfig)
{
    mRuntimeConfig.mConfig->set("workingDir", "/custom/working/dir");
    mRuntimeConfig.mConfig->set("loader", "/custom/loader/path");
    mRuntimeConfig.mConfig->set("detectMode", "auto");

    mRuntimeConfig.mConfig->set("partitions", Poco::JSON::Array::Ptr(new Poco::JSON::Array));
    mRuntimeConfig.mConfig->getArray("partitions")->add("part1");
    mRuntimeConfig.mConfig->getArray("partitions")->add("part2");

    mRuntimeConfig.mConfig->set("healthCheckServices", Poco::JSON::Array::Ptr(new Poco::JSON::Array));
    mRuntimeConfig.mConfig->getArray("healthCheckServices")->add("service1");
    mRuntimeConfig.mConfig->getArray("healthCheckServices")->add("service2");

    mRuntimeConfig.mConfig->set("versionFile", "/custom/version/file");

    BootConfig bootConfig;

    auto err = ParseConfig(mRuntimeConfig, bootConfig);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(bootConfig.mWorkingDir, "/custom/working/dir");
    EXPECT_EQ(bootConfig.mLoader, "/custom/loader/path");
    EXPECT_EQ(bootConfig.mDetectMode, BootDetectModeEnum::eAuto);

    ASSERT_EQ(bootConfig.mPartitions.size(), 2);
    EXPECT_EQ(bootConfig.mPartitions[0], "part1");
    EXPECT_EQ(bootConfig.mPartitions[1], "part2");

    ASSERT_EQ(bootConfig.mHealthCheckServices.size(), 2);
    EXPECT_EQ(bootConfig.mHealthCheckServices[0], "service1");
    EXPECT_EQ(bootConfig.mHealthCheckServices[1], "service2");

    EXPECT_EQ(bootConfig.mVersionFile, "/custom/version/file");
}

} // namespace aos::sm::launcher
