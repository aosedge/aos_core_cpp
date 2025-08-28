/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>

#include <common/cloudprotocol/desiredstatus.hpp>
#include <common/utils/json.hpp>

using namespace testing;

namespace aos::common::cloudprotocol {

namespace {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

AlertRules CreateAlerts()
{
    AlertRules alerts;

    alerts.mRAM.SetValue(AlertRulePercents {aos::Time::cSeconds, 0.1, 0.2});
    alerts.mCPU.SetValue(AlertRulePercents {2 * aos::Time::cSeconds, 0.3, 0.4});
    alerts.mPartitions.EmplaceBack(PartitionAlertRule {3 * aos::Time::cSeconds, 0.5, 0.6, "partition1"});
    alerts.mPartitions.EmplaceBack(PartitionAlertRule {4 * aos::Time::cSeconds, 0.6, 0.7, "partition2"});
    alerts.mDownload.SetValue(AlertRulePoints {5 * aos::Time::cSeconds, 100, 200});
    alerts.mUpload.SetValue(AlertRulePoints {6 * aos::Time::cSeconds, 300, 400});

    return alerts;
}

aos::cloudprotocol::ResourceRatios CreateResourceRatios()
{
    aos::cloudprotocol::ResourceRatios ratios;

    ratios.mCPU.SetValue(50);
    ratios.mRAM.SetValue(51);
    ratios.mStorage.SetValue(52);
    ratios.mState.SetValue(53);

    return ratios;
}

std::unique_ptr<aos::cloudprotocol::NodeConfig> CreateNodeConfig()
{
    auto nodeConfig = std::make_unique<aos::cloudprotocol::NodeConfig>();

    nodeConfig->mNode.EmplaceValue();
    nodeConfig->mNode->mURN.SetValue("nodeURN");
    nodeConfig->mNodeGroupSubject.mURN.SetValue("nodeGroupSubjectURN");

    nodeConfig->mAlertRules.SetValue(CreateAlerts());
    nodeConfig->mResourceRatios.SetValue(CreateResourceRatios());
    nodeConfig->mLabels.PushBack("mainNode");
    nodeConfig->mPriority = 1;

    return nodeConfig;
}

void CompareNodeConfig(
    const aos::cloudprotocol::NodeConfig& nodeConfig, const aos::cloudprotocol::NodeConfig& expectedNodeConfig)
{
    EXPECT_EQ(nodeConfig.mNode, expectedNodeConfig.mNode) << "Node ID mismatch";
    EXPECT_EQ(nodeConfig.mNodeGroupSubject, expectedNodeConfig.mNodeGroupSubject) << "Node group subject mismatch";

    EXPECT_EQ(nodeConfig.mPriority, expectedNodeConfig.mPriority) << "Priority mismatch";

    EXPECT_EQ(nodeConfig.mLabels, expectedNodeConfig.mLabels) << "Node labels mismatch";

    // Compare alert rules

    ASSERT_TRUE(nodeConfig.mAlertRules.HasValue()) << "Alert rules not set";
    ASSERT_TRUE(expectedNodeConfig.mAlertRules.HasValue()) << "Expected alert rules not set";

    EXPECT_EQ(nodeConfig.mAlertRules->mRAM, expectedNodeConfig.mAlertRules->mRAM) << "Alert rules ram mismatch";
    EXPECT_EQ(nodeConfig.mAlertRules->mCPU, expectedNodeConfig.mAlertRules->mCPU) << "Alert rules cpu mismatch";
    EXPECT_EQ(nodeConfig.mAlertRules->mPartitions, expectedNodeConfig.mAlertRules->mPartitions)
        << "Alert rules partitions mismatch";
    EXPECT_EQ(nodeConfig.mAlertRules->mDownload, expectedNodeConfig.mAlertRules->mDownload)
        << "Alert rules download mismatch";
    EXPECT_EQ(nodeConfig.mAlertRules->mUpload, expectedNodeConfig.mAlertRules->mUpload)
        << "Alert rules upload mismatch";

    // Compare resource ratios

    ASSERT_TRUE(nodeConfig.mResourceRatios.HasValue()) << "Resource ratios not set";
    ASSERT_TRUE(expectedNodeConfig.mResourceRatios.HasValue()) << "Expected resource ratios not set";
    EXPECT_EQ(nodeConfig.mResourceRatios->mCPU, expectedNodeConfig.mResourceRatios->mCPU)
        << "Resource ratios cpu mismatch";
    EXPECT_EQ(nodeConfig.mResourceRatios->mRAM, expectedNodeConfig.mResourceRatios->mRAM)
        << "Resource ratios ram mismatch";
    EXPECT_EQ(nodeConfig.mResourceRatios->mStorage, expectedNodeConfig.mResourceRatios->mStorage)
        << "Resource ratios storage mismatch";
    EXPECT_EQ(nodeConfig.mResourceRatios->mState, expectedNodeConfig.mResourceRatios->mState)
        << "Resource ratios state mismatch";
}

} // namespace

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class CloudProtocolDesiredStatus : public Test {
public:
    void SetUp() override { tests::utils::InitLog(); }
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(CloudProtocolDesiredStatus, NodeConfig)
{
    auto nodeConfig = CreateNodeConfig();

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(*nodeConfig, *json);
    ASSERT_TRUE(err.IsNone()) << "Failed to convert node config to JSON: " << aos::tests::utils::ErrorToStr(err);

    auto parsedNodeConfig = std::make_unique<aos::cloudprotocol::NodeConfig>();

    err = FromJSON(utils::CaseInsensitiveObjectWrapper(json), *parsedNodeConfig);
    ASSERT_TRUE(err.IsNone()) << "Failed to parse node config from JSON: " << aos::tests::utils::ErrorToStr(err);

    CompareNodeConfig(*parsedNodeConfig, *nodeConfig);
}

TEST_F(CloudProtocolDesiredStatus, NodeConfigFromJSONFailsOnLabelsExceedsLimit)
{
    auto parsedNodeConfig = std::make_unique<aos::cloudprotocol::NodeConfig>();

    parsedNodeConfig->mLabels.Resize(cMaxNumNodeLabels);

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    json->set("nodeGroupSubject", Poco::makeShared<Poco::JSON::Object>());
    json->set("labels",
        utils::ToJsonArray(
            std::vector<std::string> {std::string(cMaxNumNodeLabels, 'l')}, [](const auto& str) { return str; }));

    auto err = FromJSON(utils::CaseInsensitiveObjectWrapper(json), *parsedNodeConfig);
    ASSERT_EQ(err, ErrorEnum::eNoMemory);
}

TEST_F(CloudProtocolDesiredStatus, EmptyDesiredStatus)
{
    auto desiredStatus = std::make_unique<aos::cloudprotocol::DesiredStatus>();

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(*desiredStatus, *json);
    ASSERT_TRUE(err.IsNone()) << "Failed to convert desired status to JSON: " << aos::tests::utils::ErrorToStr(err);

    auto wrapper = utils::CaseInsensitiveObjectWrapper(json);

    EXPECT_EQ(wrapper.GetValue<std::string>("messageType"), "desiredStatus") << "Message type mismatch in JSON";
    EXPECT_FALSE(wrapper.Has("unitConfig")) << "Unit config should not be present in empty desired status";

    auto parsedDesiredStatus = std::make_unique<aos::cloudprotocol::DesiredStatus>();

    err = FromJSON(wrapper, *parsedDesiredStatus);
    ASSERT_TRUE(err.IsNone()) << "Failed to parse desired status from JSON: " << aos::tests::utils::ErrorToStr(err);

    EXPECT_EQ(*desiredStatus, *parsedDesiredStatus) << "Parsed desired status does not match original";
}

TEST_F(CloudProtocolDesiredStatus, DesiredStatus)
{
    auto desiredStatus = std::make_unique<aos::cloudprotocol::DesiredStatus>();

    desiredStatus->mUnitConfig.EmplaceValue();
    desiredStatus->mUnitConfig->mFormatVersion = "0.0.1";
    desiredStatus->mUnitConfig->mVersion       = "1.0.0";

    desiredStatus->mUnitConfig->mNodes.EmplaceBack();
    desiredStatus->mUnitConfig->mNodes[0] = *CreateNodeConfig();

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(*desiredStatus, *json);
    ASSERT_TRUE(err.IsNone()) << "Failed to convert desired status to JSON: " << aos::tests::utils::ErrorToStr(err);

    auto wrapper = utils::CaseInsensitiveObjectWrapper(json);

    EXPECT_EQ(wrapper.GetValue<std::string>("messageType"), "desiredStatus") << "Message type mismatch in JSON";
    EXPECT_TRUE(wrapper.Has("unitConfig")) << "Unit config expected";

    auto parsedDesiredStatus = std::make_unique<aos::cloudprotocol::DesiredStatus>();

    err = FromJSON(wrapper, *parsedDesiredStatus);
    ASSERT_TRUE(err.IsNone()) << "Failed to parse desired status from JSON: " << aos::tests::utils::ErrorToStr(err);

    EXPECT_EQ(*desiredStatus, *parsedDesiredStatus) << "Parsed desired status does not match original";
}

} // namespace aos::common::cloudprotocol
