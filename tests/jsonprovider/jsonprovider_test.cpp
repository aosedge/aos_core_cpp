/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <aos/test/log.hpp>

#include "jsonprovider/jsonprovider.hpp"

using namespace testing;

namespace aos::common::jsonprovider {

namespace {

/***********************************************************************************************************************
 * Consts
 **********************************************************************************************************************/

constexpr auto cTestNodeConfigJSON = R"({
    "devices": [
        {
            "groups": [
                "group1",
                "group2"
            ],
            "hostDevices": [
                "hostDevice1",
                "hostDevice2"
            ],
            "name": "device1",
            "sharedCount": 1
        },
        {
            "groups": [
                "group3",
                "group4"
            ],
            "hostDevices": [
                "hostDevice3",
                "hostDevice4"
            ],
            "name": "device2",
            "sharedCount": 2
        }
    ],
    "resources": [
        {
            "name": "resource1",
            "groups": ["g1", "g2"],
            "mounts": [
                {
                    "destination": "d1",
                    "type": "type1",
                    "source": "source1",
                    "options": ["option1", "option2"]
                },
                {
                    "destination": "d2",
                    "type": "type2",
                    "source": "source2",
                    "options": ["option3", "option4"]
                }
            ],
            "env": ["env1", "env2"],
            "hosts": [
                {
                    "ip": "10.0.0.100",
                    "hostName": "host1"
                },
                {
                    "ip": "10.0.0.101",
                    "hostName": "host2"
                }
            ]
        },
        {
            "name": "resource2",
            "groups": ["g3", "g4"],
            "mounts": [
                {
                    "destination": "d3",
                    "type": "type3",
                    "source": "source3",
                    "options": ["option5", "option6"]
                },
                {
                    "destination": "d4",
                    "type": "type4",
                    "source": "source4",
                    "options": ["option7", "option8"]
                }
            ],
            "env": ["env3", "env4"],
            "hosts": [
                {
                    "ip": "10.0.0.102",
                    "hostName": "host3"
                },
                {
                    "ip": "10.0.0.103",
                    "hostName": "host4"
                }
            ]
        }
    ],
    "labels": [
        "mainNode"
    ],
    "alertRules": {
        "ram": {
            "minTimeout": "PT1S",
            "minThreshold": 0.1,
            "maxThreshold": 0.2
        },
        "cpu": {
            "minTimeout": "PT2S",
            "minThreshold": 0.3,
            "maxThreshold": 0.4
        },
        "partitions": [
            {
                "name": "partition1",
                "minTimeout": "PT3S",
                "minThreshold": 0.5,
                "maxThreshold": 0.6
            },
            {
                "name": "partition2",
                "minTimeout": "PT4S",
                "minThreshold": 0.6,
                "maxThreshold": 0.7
            }
        ],
        "download": {
            "minTimeout": "PT5S",
            "minThreshold": 100,
            "maxThreshold": 200
        },
        "upload": {
            "minTimeout": "PT6S",
            "minThreshold": 300,
            "maxThreshold": 400
        }
    },
    "nodeType": "mainType",
    "priority": 1,
    "version": "1.0.0"
}

)";

constexpr auto cNodeConfigLabelOverflowBuffer = R"({
    "labels": [
        "label that is expected to trigger no memory error due to its length"
    ],
    "nodeType": "mainType",
    "priority": 1,
    "version": "1.0.0"
}
)";

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

sm::resourcemanager::NodeConfig CreateNodeConfig()
{
    sm::resourcemanager::NodeConfig nodeConfig;

    nodeConfig.mVersion              = "1.0.0";
    nodeConfig.mNodeConfig.mPriority = 1;
    nodeConfig.mNodeConfig.mNodeType = "mainType";

    nodeConfig.mNodeConfig.mDevices.Resize(2);

    nodeConfig.mNodeConfig.mDevices[0].mName        = "device1";
    nodeConfig.mNodeConfig.mDevices[0].mSharedCount = 1;
    nodeConfig.mNodeConfig.mDevices[0].mGroups.PushBack("group1");
    nodeConfig.mNodeConfig.mDevices[0].mGroups.PushBack("group2");
    nodeConfig.mNodeConfig.mDevices[0].mHostDevices.PushBack("hostDevice1");
    nodeConfig.mNodeConfig.mDevices[0].mHostDevices.PushBack("hostDevice2");

    nodeConfig.mNodeConfig.mDevices[1].mName        = "device2";
    nodeConfig.mNodeConfig.mDevices[1].mSharedCount = 2;
    nodeConfig.mNodeConfig.mDevices[1].mGroups.PushBack("group3");
    nodeConfig.mNodeConfig.mDevices[1].mGroups.PushBack("group4");
    nodeConfig.mNodeConfig.mDevices[1].mHostDevices.PushBack("hostDevice3");
    nodeConfig.mNodeConfig.mDevices[1].mHostDevices.PushBack("hostDevice4");

    nodeConfig.mNodeConfig.mResources.Resize(2);

    nodeConfig.mNodeConfig.mResources[0].mName = "resource1";
    nodeConfig.mNodeConfig.mResources[0].mGroups.PushBack("g1");
    nodeConfig.mNodeConfig.mResources[0].mGroups.PushBack("g2");

    nodeConfig.mNodeConfig.mResources[0].mMounts.Resize(2);
    nodeConfig.mNodeConfig.mResources[0].mMounts[0].mDestination = "d1";
    nodeConfig.mNodeConfig.mResources[0].mMounts[0].mType        = "type1";
    nodeConfig.mNodeConfig.mResources[0].mMounts[0].mSource      = "source1";
    nodeConfig.mNodeConfig.mResources[0].mMounts[0].mOptions.PushBack("option1");
    nodeConfig.mNodeConfig.mResources[0].mMounts[0].mOptions.PushBack("option2");

    nodeConfig.mNodeConfig.mResources[0].mMounts[1].mDestination = "d2";
    nodeConfig.mNodeConfig.mResources[0].mMounts[1].mType        = "type2";
    nodeConfig.mNodeConfig.mResources[0].mMounts[1].mSource      = "source2";
    nodeConfig.mNodeConfig.mResources[0].mMounts[1].mOptions.PushBack("option3");
    nodeConfig.mNodeConfig.mResources[0].mMounts[1].mOptions.PushBack("option4");

    nodeConfig.mNodeConfig.mResources[0].mEnv.PushBack("env1");
    nodeConfig.mNodeConfig.mResources[0].mEnv.PushBack("env2");

    nodeConfig.mNodeConfig.mResources[0].mHosts.Resize(2);
    nodeConfig.mNodeConfig.mResources[0].mHosts[0].mIP       = "10.0.0.100";
    nodeConfig.mNodeConfig.mResources[0].mHosts[0].mHostname = "host1";

    nodeConfig.mNodeConfig.mResources[0].mHosts[1].mIP       = "10.0.0.101";
    nodeConfig.mNodeConfig.mResources[0].mHosts[1].mHostname = "host2";

    nodeConfig.mNodeConfig.mResources[1].mName = "resource2";
    nodeConfig.mNodeConfig.mResources[1].mGroups.PushBack("g3");
    nodeConfig.mNodeConfig.mResources[1].mGroups.PushBack("g4");

    nodeConfig.mNodeConfig.mResources[1].mMounts.Resize(2);
    nodeConfig.mNodeConfig.mResources[1].mMounts[0].mDestination = "d3";
    nodeConfig.mNodeConfig.mResources[1].mMounts[0].mType        = "type3";
    nodeConfig.mNodeConfig.mResources[1].mMounts[0].mSource      = "source3";
    nodeConfig.mNodeConfig.mResources[1].mMounts[0].mOptions.PushBack("option5");
    nodeConfig.mNodeConfig.mResources[1].mMounts[0].mOptions.PushBack("option6");

    nodeConfig.mNodeConfig.mResources[1].mMounts[1].mDestination = "d4";
    nodeConfig.mNodeConfig.mResources[1].mMounts[1].mType        = "type4";
    nodeConfig.mNodeConfig.mResources[1].mMounts[1].mSource      = "source4";
    nodeConfig.mNodeConfig.mResources[1].mMounts[1].mOptions.PushBack("option7");
    nodeConfig.mNodeConfig.mResources[1].mMounts[1].mOptions.PushBack("option8");

    nodeConfig.mNodeConfig.mResources[1].mEnv.PushBack("env3");
    nodeConfig.mNodeConfig.mResources[1].mEnv.PushBack("env4");

    nodeConfig.mNodeConfig.mResources[1].mHosts.Resize(2);
    nodeConfig.mNodeConfig.mResources[1].mHosts[0].mIP       = "10.0.0.102";
    nodeConfig.mNodeConfig.mResources[1].mHosts[0].mHostname = "host3";
    nodeConfig.mNodeConfig.mResources[1].mHosts[1].mIP       = "10.0.0.103";
    nodeConfig.mNodeConfig.mResources[1].mHosts[1].mHostname = "host4";

    nodeConfig.mNodeConfig.mLabels.PushBack("mainNode");

    nodeConfig.mNodeConfig.mAlertRules.SetValue(CreateAlerts());

    return nodeConfig;
}

void CompareNodeConfig(
    const sm::resourcemanager::NodeConfig& nodeConfig, const sm::resourcemanager::NodeConfig& expectedNodeConfig)
{
    EXPECT_EQ(nodeConfig.mVersion, expectedNodeConfig.mVersion) << "Version mismatch";
    EXPECT_EQ(nodeConfig.mNodeConfig.mNodeType, expectedNodeConfig.mNodeConfig.mNodeType) << "Node type mismatch";
    EXPECT_EQ(nodeConfig.mNodeConfig.mPriority, expectedNodeConfig.mNodeConfig.mPriority) << "Priority mismatch";

    EXPECT_EQ(nodeConfig.mNodeConfig.mDevices, expectedNodeConfig.mNodeConfig.mDevices) << "Device info mismatch";
    EXPECT_EQ(nodeConfig.mNodeConfig.mLabels, expectedNodeConfig.mNodeConfig.mLabels) << "Node labels mismatch";

    // Compare resources

    ASSERT_EQ(nodeConfig.mNodeConfig.mResources.Size(), expectedNodeConfig.mNodeConfig.mResources.Size())
        << "Resources size mismatch";

    for (size_t i = 0; i < nodeConfig.mNodeConfig.mResources.Size(); ++i) {
        const auto& resource         = nodeConfig.mNodeConfig.mResources[i];
        const auto& expectedResource = expectedNodeConfig.mNodeConfig.mResources[i];

        EXPECT_EQ(resource.mName, expectedResource.mName) << "Resource name mismatch";
        EXPECT_EQ(resource.mGroups, expectedResource.mGroups) << "Resource groups mismatch";
        EXPECT_EQ(resource.mMounts, expectedResource.mMounts) << "Resource mounts mismatch";
        EXPECT_EQ(resource.mEnv, expectedResource.mEnv) << "Resource envs mismatch";
        EXPECT_EQ(resource.mHosts, expectedResource.mHosts) << "Resource hosts mismatch";
    }

    // Compare alert rules

    ASSERT_TRUE(nodeConfig.mNodeConfig.mAlertRules.HasValue()) << "Alert rules not set";
    ASSERT_TRUE(expectedNodeConfig.mNodeConfig.mAlertRules.HasValue()) << "Expected alert rules not set";

    EXPECT_EQ(nodeConfig.mNodeConfig.mAlertRules->mRAM, expectedNodeConfig.mNodeConfig.mAlertRules->mRAM)
        << "Alert rules ram mismatch";
    EXPECT_EQ(nodeConfig.mNodeConfig.mAlertRules->mCPU, expectedNodeConfig.mNodeConfig.mAlertRules->mCPU)
        << "Alert rules cpu mismatch";
    EXPECT_EQ(nodeConfig.mNodeConfig.mAlertRules->mPartitions, expectedNodeConfig.mNodeConfig.mAlertRules->mPartitions)
        << "Alert rules partitions mismatch";
    EXPECT_EQ(nodeConfig.mNodeConfig.mAlertRules->mDownload, expectedNodeConfig.mNodeConfig.mAlertRules->mDownload)
        << "Alert rules download mismatch";
    EXPECT_EQ(nodeConfig.mNodeConfig.mAlertRules->mUpload, expectedNodeConfig.mNodeConfig.mAlertRules->mUpload)
        << "Alert rules upload mismatch";
}

} // namespace

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/
class JSONProviderTest : public Test {
public:
    void SetUp() override { test::InitLog(); }

    JSONProvider mProvider;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(JSONProviderTest, NodeConfigFromJSONSucceeds)
{
    auto parsedNodeConfig = std::make_unique<sm::resourcemanager::NodeConfig>();

    ASSERT_EQ(mProvider.NodeConfigFromJSON(cTestNodeConfigJSON, *parsedNodeConfig), ErrorEnum::eNone);

    CompareNodeConfig(*parsedNodeConfig, CreateNodeConfig());
}

TEST_F(JSONProviderTest, NodeConfigFromJSONFailsOnHostDevicesExceedsLimit)
{
    auto parsedNodeConfig = std::make_unique<sm::resourcemanager::NodeConfig>();

    parsedNodeConfig->mNodeConfig.mDevices.Resize(cMaxNumNodeDevices);

    ASSERT_EQ(mProvider.NodeConfigFromJSON(cTestNodeConfigJSON, *parsedNodeConfig), ErrorEnum::eNoMemory);
}

TEST_F(JSONProviderTest, NodeConfigFromJSONFailsOnResourcesExceedsLimit)
{
    auto parsedNodeConfig = std::make_unique<sm::resourcemanager::NodeConfig>();

    parsedNodeConfig->mNodeConfig.mResources.Resize(cMaxNumNodeResources);

    ASSERT_EQ(mProvider.NodeConfigFromJSON(cTestNodeConfigJSON, *parsedNodeConfig), ErrorEnum::eNoMemory);
}

TEST_F(JSONProviderTest, NodeConfigFromJSONFailsOnLabelsExceedsLimit)
{
    auto parsedNodeConfig = std::make_unique<sm::resourcemanager::NodeConfig>();

    parsedNodeConfig->mNodeConfig.mLabels.Resize(cMaxNumNodeLabels);

    ASSERT_EQ(mProvider.NodeConfigFromJSON(cTestNodeConfigJSON, *parsedNodeConfig), ErrorEnum::eNoMemory);

    parsedNodeConfig = std::make_unique<sm::resourcemanager::NodeConfig>();

    ASSERT_EQ(mProvider.NodeConfigFromJSON(cNodeConfigLabelOverflowBuffer, *parsedNodeConfig), ErrorEnum::eNoMemory);
}

TEST_F(JSONProviderTest, NodeConfigToJSON)
{
    const sm::resourcemanager::NodeConfig nodeConfig = CreateNodeConfig();
    auto nodeConfigJSON   = std::make_unique<StaticString<sm::resourcemanager::cNodeConfigJSONLen>>();
    auto parsedNodeConfig = std::make_unique<sm::resourcemanager::NodeConfig>();

    ASSERT_EQ(mProvider.NodeConfigToJSON(nodeConfig, *nodeConfigJSON), ErrorEnum::eNone);

    ASSERT_EQ(mProvider.NodeConfigFromJSON(cTestNodeConfigJSON, *parsedNodeConfig), ErrorEnum::eNone);

    CompareNodeConfig(*parsedNodeConfig, nodeConfig);
}

} // namespace aos::common::jsonprovider
