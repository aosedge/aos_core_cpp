/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <aos/test/log.hpp>
#include <aos/test/utils.hpp>

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

ResourceRatios CreateResourceRatios()
{
    ResourceRatios ratios;

    ratios.mCPU.SetValue(50);
    ratios.mRAM.SetValue(51);
    ratios.mStorage.SetValue(52);
    ratios.mState.SetValue(53);

    return ratios;
}

std::unique_ptr<aos::cloudprotocol::NodeConfig> CreateNodeConfig()
{
    auto nodeConfig = std::make_unique<aos::cloudprotocol::NodeConfig>();

    nodeConfig->mPriority = 1;
    nodeConfig->mNodeType = "mainType";

    nodeConfig->mDevices.Resize(2);

    nodeConfig->mDevices[0].mName        = "device1";
    nodeConfig->mDevices[0].mSharedCount = 1;
    nodeConfig->mDevices[0].mGroups.PushBack("group1");
    nodeConfig->mDevices[0].mGroups.PushBack("group2");
    nodeConfig->mDevices[0].mHostDevices.PushBack("hostDevice1");
    nodeConfig->mDevices[0].mHostDevices.PushBack("hostDevice2");

    nodeConfig->mDevices[1].mName        = "device2";
    nodeConfig->mDevices[1].mSharedCount = 2;
    nodeConfig->mDevices[1].mGroups.PushBack("group3");
    nodeConfig->mDevices[1].mGroups.PushBack("group4");
    nodeConfig->mDevices[1].mHostDevices.PushBack("hostDevice3");
    nodeConfig->mDevices[1].mHostDevices.PushBack("hostDevice4");

    nodeConfig->mResources.Resize(2);

    nodeConfig->mResources[0].mName = "resource1";
    nodeConfig->mResources[0].mGroups.PushBack("g1");
    nodeConfig->mResources[0].mGroups.PushBack("g2");

    nodeConfig->mResources[0].mMounts.Resize(2);
    nodeConfig->mResources[0].mMounts[0].mDestination = "d1";
    nodeConfig->mResources[0].mMounts[0].mType        = "type1";
    nodeConfig->mResources[0].mMounts[0].mSource      = "source1";
    nodeConfig->mResources[0].mMounts[0].mOptions.PushBack("option1");
    nodeConfig->mResources[0].mMounts[0].mOptions.PushBack("option2");

    nodeConfig->mResources[0].mMounts[1].mDestination = "d2";
    nodeConfig->mResources[0].mMounts[1].mType        = "type2";
    nodeConfig->mResources[0].mMounts[1].mSource      = "source2";
    nodeConfig->mResources[0].mMounts[1].mOptions.PushBack("option3");
    nodeConfig->mResources[0].mMounts[1].mOptions.PushBack("option4");

    nodeConfig->mResources[0].mEnv.PushBack("env1");
    nodeConfig->mResources[0].mEnv.PushBack("env2");

    nodeConfig->mResources[0].mHosts.Resize(2);
    nodeConfig->mResources[0].mHosts[0].mIP       = "10.0.0.100";
    nodeConfig->mResources[0].mHosts[0].mHostname = "host1";

    nodeConfig->mResources[0].mHosts[1].mIP       = "10.0.0.101";
    nodeConfig->mResources[0].mHosts[1].mHostname = "host2";

    nodeConfig->mResources[1].mName = "resource2";
    nodeConfig->mResources[1].mGroups.PushBack("g3");
    nodeConfig->mResources[1].mGroups.PushBack("g4");

    nodeConfig->mResources[1].mMounts.Resize(2);
    nodeConfig->mResources[1].mMounts[0].mDestination = "d3";
    nodeConfig->mResources[1].mMounts[0].mType        = "type3";
    nodeConfig->mResources[1].mMounts[0].mSource      = "source3";
    nodeConfig->mResources[1].mMounts[0].mOptions.PushBack("option5");
    nodeConfig->mResources[1].mMounts[0].mOptions.PushBack("option6");

    nodeConfig->mResources[1].mMounts[1].mDestination = "d4";
    nodeConfig->mResources[1].mMounts[1].mType        = "type4";
    nodeConfig->mResources[1].mMounts[1].mSource      = "source4";
    nodeConfig->mResources[1].mMounts[1].mOptions.PushBack("option7");
    nodeConfig->mResources[1].mMounts[1].mOptions.PushBack("option8");

    nodeConfig->mResources[1].mEnv.PushBack("env3");
    nodeConfig->mResources[1].mEnv.PushBack("env4");

    nodeConfig->mResources[1].mHosts.Resize(2);
    nodeConfig->mResources[1].mHosts[0].mIP       = "10.0.0.102";
    nodeConfig->mResources[1].mHosts[0].mHostname = "host3";
    nodeConfig->mResources[1].mHosts[1].mIP       = "10.0.0.103";
    nodeConfig->mResources[1].mHosts[1].mHostname = "host4";

    nodeConfig->mLabels.PushBack("mainNode");

    nodeConfig->mAlertRules.SetValue(CreateAlerts());

    nodeConfig->mResourceRatios.SetValue(CreateResourceRatios());

    return nodeConfig;
}

void CompareNodeConfig(
    const aos::cloudprotocol::NodeConfig& nodeConfig, const aos::cloudprotocol::NodeConfig& expectedNodeConfig)
{
    EXPECT_EQ(nodeConfig.mNodeType, expectedNodeConfig.mNodeType) << "Node type mismatch";
    EXPECT_EQ(nodeConfig.mPriority, expectedNodeConfig.mPriority) << "Priority mismatch";

    EXPECT_EQ(nodeConfig.mDevices, expectedNodeConfig.mDevices) << "Device info mismatch";
    EXPECT_EQ(nodeConfig.mLabels, expectedNodeConfig.mLabels) << "Node labels mismatch";

    // Compare resources

    ASSERT_EQ(nodeConfig.mResources.Size(), expectedNodeConfig.mResources.Size()) << "Resources size mismatch";

    for (size_t i = 0; i < nodeConfig.mResources.Size(); ++i) {
        const auto& resource         = nodeConfig.mResources[i];
        const auto& expectedResource = expectedNodeConfig.mResources[i];

        EXPECT_EQ(resource.mName, expectedResource.mName) << "Resource name mismatch";
        EXPECT_EQ(resource.mGroups, expectedResource.mGroups) << "Resource groups mismatch";
        EXPECT_EQ(resource.mMounts, expectedResource.mMounts) << "Resource mounts mismatch";
        EXPECT_EQ(resource.mEnv, expectedResource.mEnv) << "Resource envs mismatch";
        EXPECT_EQ(resource.mHosts, expectedResource.mHosts) << "Resource hosts mismatch";
    }

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
    void SetUp() override { test::InitLog(); }
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(CloudProtocolDesiredStatus, NodeConfig)
{
    auto nodeConfig = CreateNodeConfig();

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(*nodeConfig, *json);
    ASSERT_TRUE(err.IsNone()) << "Failed to convert node config to JSON: " << aos::test::ErrorToStr(err);

    auto parsedNodeConfig = std::make_unique<aos::cloudprotocol::NodeConfig>();

    err = FromJSON(utils::CaseInsensitiveObjectWrapper(json), *parsedNodeConfig);
    ASSERT_TRUE(err.IsNone()) << "Failed to parse node config from JSON: " << aos::test::ErrorToStr(err);

    CompareNodeConfig(*parsedNodeConfig, *nodeConfig);
}

TEST_F(CloudProtocolDesiredStatus, NodeConfigFromJSONFailsOnHostDevicesExceedsLimit)
{
    auto parsedNodeConfig = std::make_unique<aos::cloudprotocol::NodeConfig>();

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    json->set("devices", utils::ToJsonArray(std::vector<std::string> {"dev1"}, [](const auto& str) { return str; }));

    parsedNodeConfig->mDevices.Resize(cMaxNumNodeDevices);

    auto err = FromJSON(utils::CaseInsensitiveObjectWrapper(json), *parsedNodeConfig);
    ASSERT_EQ(err, ErrorEnum::eNoMemory);
}

TEST_F(CloudProtocolDesiredStatus, NodeConfigFromJSONFailsOnResourcesExceedsLimit)
{
    auto parsedNodeConfig = std::make_unique<aos::cloudprotocol::NodeConfig>();

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    json->set("resources", utils::ToJsonArray(std::vector<std::string> {"res1"}, [](const auto& str) { return str; }));

    parsedNodeConfig->mResources.Resize(cMaxNumNodeResources);

    auto err = FromJSON(utils::CaseInsensitiveObjectWrapper(json), *parsedNodeConfig);
    ASSERT_EQ(err, ErrorEnum::eNoMemory);
}

TEST_F(CloudProtocolDesiredStatus, NodeConfigFromJSONFailsOnLabelsExceedsLimit)
{
    auto parsedNodeConfig = std::make_unique<aos::cloudprotocol::NodeConfig>();

    parsedNodeConfig->mLabels.Resize(cMaxNumNodeLabels);

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

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
    ASSERT_TRUE(err.IsNone()) << "Failed to convert desired status to JSON: " << aos::test::ErrorToStr(err);

    auto wrapper = utils::CaseInsensitiveObjectWrapper(json);

    EXPECT_EQ(wrapper.GetValue<std::string>("messageType"), "desiredStatus") << "Message type mismatch in JSON";
    EXPECT_FALSE(wrapper.Has("unitConfig")) << "Unit config should not be present in empty desired status";

    auto parsedDesiredStatus = std::make_unique<aos::cloudprotocol::DesiredStatus>();

    err = FromJSON(wrapper, *parsedDesiredStatus);
    ASSERT_TRUE(err.IsNone()) << "Failed to parse desired status from JSON: " << aos::test::ErrorToStr(err);

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
    ASSERT_TRUE(err.IsNone()) << "Failed to convert desired status to JSON: " << aos::test::ErrorToStr(err);

    auto wrapper = utils::CaseInsensitiveObjectWrapper(json);

    EXPECT_EQ(wrapper.GetValue<std::string>("messageType"), "desiredStatus") << "Message type mismatch in JSON";
    EXPECT_TRUE(wrapper.Has("unitConfig")) << "Unit config expected";

    auto parsedDesiredStatus = std::make_unique<aos::cloudprotocol::DesiredStatus>();

    err = FromJSON(wrapper, *parsedDesiredStatus);
    ASSERT_TRUE(err.IsNone()) << "Failed to parse desired status from JSON: " << aos::test::ErrorToStr(err);

    EXPECT_EQ(*desiredStatus, *parsedDesiredStatus) << "Parsed desired status does not match original";
}

} // namespace aos::common::cloudprotocol
