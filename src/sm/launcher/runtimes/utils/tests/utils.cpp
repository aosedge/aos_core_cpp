/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>

#include <common/utils/utils.hpp>

#include <sm/launcher/runtimes/utils/utils.hpp>

using namespace testing;

namespace aos::sm::launcher::utils {

class RuntimeUtilsTests : public Test {
protected:
    static void SetUpTestSuite() { tests::utils::InitLog(); }
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(RuntimeUtilsTests, CreateRuntimeInfo)
{
    const auto cExpectedRuntimeID = common::utils::NameUUID("runtimeType-nodeID");

    auto nodeInfo       = std::make_unique<NodeInfo>();
    nodeInfo->mNodeID   = "nodeID";
    nodeInfo->mNodeType = "nodeType";

    nodeInfo->mCPUs.EmplaceBack();
    nodeInfo->mCPUs[0].mArchInfo.mArchitecture = "amd64";

    nodeInfo->mOSInfo.mOS = "linux";

    auto runtimeInfo = std::make_unique<RuntimeInfo>();

    auto err = CreateRuntimeInfo("runtimeType", *nodeInfo, 2, *runtimeInfo);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(runtimeInfo->mMaxInstances, 2u);

    EXPECT_STREQ(runtimeInfo->mRuntimeType.CStr(), "runtimeType");
    EXPECT_EQ(runtimeInfo->mMaxInstances, 2u);
    EXPECT_STREQ(runtimeInfo->mRuntimeID.CStr(), cExpectedRuntimeID.c_str());

    EXPECT_STREQ(runtimeInfo->mArchInfo.mArchitecture.CStr(), "amd64");
    EXPECT_STREQ(runtimeInfo->mOSInfo.mOS.CStr(), "linux");
}

TEST_F(RuntimeUtilsTests, CreateRuntimeInfoErrorOnEmptyCPUInfo)
{
    auto nodeInfo         = std::make_unique<NodeInfo>();
    nodeInfo->mNodeID     = "nodeID";
    nodeInfo->mNodeType   = "nodeType";
    nodeInfo->mOSInfo.mOS = "linux";

    auto runtimeInfo = std::make_unique<RuntimeInfo>();

    auto err = CreateRuntimeInfo("runtimeType", *nodeInfo, 2, *runtimeInfo);
    ASSERT_TRUE(err.Is(ErrorEnum::eInvalidArgument)) << tests::utils::ErrorToStr(err);
}

} // namespace aos::sm::launcher::utils
