/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string>

#include <Poco/JSON/Object.h>
#include <gmock/gmock.h>

#include "visidentifier/visconfig.hpp"

using namespace testing;

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class VISConfigTest : public Test {
protected:
    Poco::JSON::Object mConfig;

    void SetUp() override
    {
        mConfig.set("visServer", "vis-server");
        mConfig.set("caCertFile", "ca-file");
        mConfig.set("webSocketTimeout", "10s");
    }
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(VISConfigTest, InitSucceeds)
{
    VISConfig visConfig;

    auto err = visConfig.Init(mConfig);
    ASSERT_TRUE(err.IsNone()) << err.Message();
}

TEST_F(VISConfigTest, InitFailsVisServerKeyNotFound)
{
    VISConfig visConfig;

    mConfig.remove("visServer");

    auto err = visConfig.Init(mConfig);
    ASSERT_TRUE(err.Is(aos::ErrorEnum::eNotFound)) << err.Message();
}

TEST_F(VISConfigTest, InitFailsVisServerInvalidFormat)
{
    VISConfig visConfig;

    mConfig.set("visServer", 100);

    auto err = visConfig.Init(mConfig);
    ASSERT_TRUE(err.Is(aos::ErrorEnum::eInvalidArgument)) << err.Message();
}

TEST_F(VISConfigTest, InitFailsInvalidWebSocketTimeout)
{
    VISConfig visConfig;

    mConfig.set("webSocketTimeout", "invalid format");

    auto err = visConfig.Init(mConfig);
    ASSERT_TRUE(err.Is(aos::ErrorEnum::eInvalidArgument)) << err.Message();
}

TEST_F(VISConfigTest, ToJSON)
{
    VISConfig visConfig;

    auto err = visConfig.Init(mConfig);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    ASSERT_FALSE(visConfig.ToJSON().isEmpty());
}

TEST_F(VISConfigTest, ToString)
{
    VISConfig visConfig;

    auto err = visConfig.Init(mConfig);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    ASSERT_FALSE(visConfig.ToString().empty());
}
