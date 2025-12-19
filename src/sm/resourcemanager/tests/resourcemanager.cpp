/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>

#include <sm/resourcemanager/resourcemanager.hpp>

using namespace testing;

namespace aos::sm::resourcemanager {

namespace {

/***********************************************************************************************************************
 * Consts
 **********************************************************************************************************************/

const auto cResourceInfoFile = std::filesystem::path("resource_info.json");

} // namespace

class ResourcemanagerTest : public Test {
public:
    void SetUp() override
    {
        tests::utils::InitLog();

        if (auto file = std::ofstream(cResourceInfoFile); !file) {
            throw std::runtime_error("can't create resource info file");
        }

        mConfig.mResourceInfoFilePath = cResourceInfoFile.c_str();
    }

    Config          mConfig;
    ResourceManager mResourceManager;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(ResourcemanagerTest, InitSucceedsNoFile)
{
    mConfig.mResourceInfoFilePath = "non_existing_file.json";

    auto err = mResourceManager.Init(mConfig);
    EXPECT_TRUE(err.IsNone());

    auto resources = std::make_unique<StaticArray<ResourceInfo, cMaxNumNodeResources>>();

    err = mResourceManager.GetResourcesInfos(*resources);
    EXPECT_TRUE(err.IsNone());
    EXPECT_EQ(resources->Size(), 0u);
}

TEST_F(ResourcemanagerTest, InitFailsInvalidFormat)
{
    if (auto file = std::ofstream(cResourceInfoFile); file) {
        file << "invalid json format";
    }

    auto err = mResourceManager.Init(mConfig);
    EXPECT_FALSE(err.IsNone());
}

TEST_F(ResourcemanagerTest, InitSucceedsEmptyList)
{
    if (auto file = std::ofstream(cResourceInfoFile); file) {
        file << "[]";
    }

    auto err = mResourceManager.Init(mConfig);
    EXPECT_TRUE(err.IsNone());

    auto resources = std::make_unique<StaticArray<ResourceInfo, cMaxNumNodeResources>>();

    err = mResourceManager.GetResourcesInfos(*resources);
    EXPECT_TRUE(err.IsNone());

    EXPECT_EQ(resources->Size(), 0u);
}

TEST_F(ResourcemanagerTest, InitSucceeds)
{
    if (auto file = std::ofstream(cResourceInfoFile); file) {
        file << R"([
            {
                "name": "name0",
                "sharedCount": 1,
                "groups": [
                    "group0",
                    "group1"
                ],
                "mounts": [
                    {
                        "destination": "destination",
                        "type": "type",
                        "source": "source",
                        "options": [
                            "option0",
                            "option1"
                        ]
                    }
                ],
                "envs": [
                    "key0=value0",
                    "key1=value1"
                ],
                "hosts": [
                    {
                        "hostname": "host0",
                        "ip": "ip0"
                    }
                ]
            },
            {
                "name": "name1",
                "sharedCount": 2
            }
        ])";
    }

    auto err = mResourceManager.Init(mConfig);
    EXPECT_TRUE(err.IsNone());

    auto resources = std::make_unique<StaticArray<ResourceInfo, cMaxNumNodeResources>>();

    err = mResourceManager.GetResourcesInfos(*resources);
    EXPECT_TRUE(err.IsNone());

    ASSERT_EQ(resources->Size(), 2u);

    const auto& resource0 = (*resources)[0];
    EXPECT_STREQ(resource0.mName.CStr(), "name0");
    EXPECT_EQ(resource0.mSharedCount, 1u);

    ASSERT_EQ(resource0.mGroups.Size(), 2u);
    EXPECT_STREQ(resource0.mGroups[0].CStr(), "group0");
    EXPECT_STREQ(resource0.mGroups[1].CStr(), "group1");

    ASSERT_EQ(resource0.mMounts.Size(), 1u);
    EXPECT_STREQ(resource0.mMounts[0].mDestination.CStr(), "destination");
    EXPECT_STREQ(resource0.mMounts[0].mType.CStr(), "type");
    EXPECT_STREQ(resource0.mMounts[0].mSource.CStr(), "source");

    ASSERT_EQ(resource0.mMounts[0].mOptions.Size(), 2u);
    EXPECT_STREQ(resource0.mMounts[0].mOptions[0].CStr(), "option0");
    EXPECT_STREQ(resource0.mMounts[0].mOptions[1].CStr(), "option1");

    ASSERT_EQ(resource0.mEnv.Size(), 2u);
    EXPECT_STREQ(resource0.mEnv[0].CStr(), "key0=value0");
    EXPECT_STREQ(resource0.mEnv[1].CStr(), "key1=value1");

    ASSERT_EQ(resource0.mHosts.Size(), 1u);
    EXPECT_STREQ(resource0.mHosts[0].mHostname.CStr(), "host0");
    EXPECT_STREQ(resource0.mHosts[0].mIP.CStr(), "ip0");

    const auto& resource1 = (*resources)[1];
    EXPECT_STREQ(resource1.mName.CStr(), "name1");
    EXPECT_EQ(resource1.mSharedCount, 2u);
    EXPECT_EQ(resource1.mGroups.Size(), 0u);
    EXPECT_EQ(resource1.mMounts.Size(), 0u);
    EXPECT_EQ(resource1.mEnv.Size(), 0u);
    EXPECT_EQ(resource1.mHosts.Size(), 0u);
}

} // namespace aos::sm::resourcemanager
