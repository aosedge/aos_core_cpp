/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <Poco/JSON/Parser.h>

#include <common/utils/filesystem.hpp>

#include <sm/launcher/runtimes/container/config.hpp>

using namespace testing;

namespace aos::sm::launcher {

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST(ContainerConfigTest, DefaultValues)
{
    ContainerConfig config;

    EXPECT_NO_THROW(ParseContainerConfig(
        common::utils::CaseInsensitiveObjectWrapper(Poco::makeShared<Poco::JSON::Object>()), "/working/dir", config));

    EXPECT_EQ(config.mRuntimeDir, "/run/aos/runtime");
    EXPECT_EQ(config.mHostWhiteoutsDir, "/working/dir/whiteouts");
    EXPECT_EQ(config.mStorageDir, "/working/dir/storages");
    EXPECT_EQ(config.mStateDir, "/working/dir/states");
    EXPECT_TRUE(config.mHostBinds.empty());
    EXPECT_TRUE(config.mHosts.empty());
}

TEST(ContainerConfigTest, ParseContainerConfig)
{
    static constexpr auto cTestConfig = R"({
        "runtimeDir": "/run/aos/container/runtime",
        "hostWhiteoutsDir": "/var/aos/whiteouts",
        "storageDir": "/var/aos/storages",
        "stateDir": "/var/aos/states",
        "hostBinds": [
            "usr",
            "lib"
        ],
        "hosts": [
            {
                "ip": "10.0.0.1",
                "hostname": "host1"
            },
            {
                "ip": "10.0.0.2",
                "hostname": "host2"
            }
        ]
    })";

    Poco::JSON::Parser                          parser;
    auto                                        result = parser.parse(cTestConfig);
    common::utils::CaseInsensitiveObjectWrapper object(result);

    ContainerConfig config;

    EXPECT_NO_THROW(ParseContainerConfig(object, "/working/dir", config));

    EXPECT_EQ(config.mRuntimeDir, "/run/aos/container/runtime");
    EXPECT_EQ(config.mHostWhiteoutsDir, "/var/aos/whiteouts");
    EXPECT_EQ(config.mStorageDir, "/var/aos/storages");
    EXPECT_EQ(config.mStateDir, "/var/aos/states");
    EXPECT_EQ(config.mHostBinds, std::vector<std::string>({"usr", "lib"}));
    EXPECT_EQ(config.mHosts,
        std::vector<Host>({
            Host {"10.0.0.1", "host1"},
            Host {"10.0.0.2", "host2"},
        }));
}

} // namespace aos::sm::launcher
