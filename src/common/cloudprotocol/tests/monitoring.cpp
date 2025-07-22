/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <aos/test/log.hpp>
#include <aos/test/utils.hpp>

#include <common/cloudprotocol/monitoring.hpp>
#include <common/utils/json.hpp>

using namespace testing;

namespace aos::common::cloudprotocol {

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class CloudProtocolMonitoring : public Test {
public:
    void SetUp() override { test::InitLog(); }
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(CloudProtocolMonitoring, EmptyMonitoring)
{
    auto monitoring = std::make_unique<aos::cloudprotocol::Monitoring>();

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(*monitoring, *json);
    ASSERT_TRUE(err.IsNone()) << "Error: " << test::ErrorToStr(err);

    auto jsonWrapper = utils::CaseInsensitiveObjectWrapper(json);

    EXPECT_EQ(jsonWrapper.GetValue<std::string>("messageType"), "monitoringData");
    EXPECT_TRUE(jsonWrapper.Has("nodes"));
    EXPECT_FALSE(jsonWrapper.Has("serviceInstances"));

    auto unparsedMonitoring = std::make_unique<aos::cloudprotocol::Monitoring>();

    err = FromJSON(jsonWrapper, *unparsedMonitoring);
    ASSERT_TRUE(err.IsNone()) << "Error: " << test::ErrorToStr(err);

    EXPECT_EQ(*monitoring, *unparsedMonitoring);
}

TEST_F(CloudProtocolMonitoring, Monitoring)
{
    auto monitoring = std::make_unique<aos::cloudprotocol::Monitoring>();

    monitoring->mNodes.EmplaceBack();
    monitoring->mNodes.Back().mNodeID = "node1";
    monitoring->mNodes.Back().mItems.EmplaceBack();
    monitoring->mNodes.Back().mItems.Back().mTime     = Time::Now();
    monitoring->mNodes.Back().mItems.Back().mCPU      = 10;
    monitoring->mNodes.Back().mItems.Back().mRAM      = 2048;
    monitoring->mNodes.Back().mItems.Back().mDownload = 1000;
    monitoring->mNodes.Back().mItems.Back().mUpload   = 500;
    monitoring->mNodes.Back().mItems.Back().mPartitions.EmplaceBack();
    monitoring->mNodes.Back().mItems.Back().mPartitions.Back().mName     = "partition1";
    monitoring->mNodes.Back().mItems.Back().mPartitions.Back().mUsedSize = 100000;

    monitoring->mNodes.EmplaceBack();
    monitoring->mNodes.Back().mNodeID = "node2";
    monitoring->mNodes.Back().mItems.EmplaceBack();
    monitoring->mNodes.Back().mItems.Back().mCPU      = 10;
    monitoring->mNodes.Back().mItems.Back().mRAM      = 2048;
    monitoring->mNodes.Back().mItems.Back().mDownload = 1000;
    monitoring->mNodes.Back().mItems.Back().mUpload   = 500;

    monitoring->mNodes.EmplaceBack();
    monitoring->mNodes.Back().mNodeID = "node3";

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(*monitoring, *json);
    ASSERT_TRUE(err.IsNone()) << "Error: " << test::ErrorToStr(err);

    auto jsonWrapper = utils::CaseInsensitiveObjectWrapper(json);

    EXPECT_EQ(jsonWrapper.GetValue<std::string>("messageType"), "monitoringData");
    EXPECT_TRUE(jsonWrapper.Has("nodes"));
    EXPECT_FALSE(jsonWrapper.Has("serviceInstances"));

    auto unparsedMonitoring = std::make_unique<aos::cloudprotocol::Monitoring>();

    err = FromJSON(jsonWrapper, *unparsedMonitoring);
    ASSERT_TRUE(err.IsNone()) << "Error: " << test::ErrorToStr(err);

    EXPECT_EQ(*monitoring, *unparsedMonitoring);
}

} // namespace aos::common::cloudprotocol
