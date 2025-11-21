/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>

#include <common/jsonprovider/jsonprovider.hpp>

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
    "resourceRatios": {
        "cpu": 50,
        "ram": 51,
        "storage": 52,
        "state": 53
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

aos::ResourceRatios CreateResourceRatios()
{
    aos::ResourceRatios ratios;

    ratios.mCPU.SetValue(50);
    ratios.mRAM.SetValue(51);
    ratios.mStorage.SetValue(52);
    ratios.mState.SetValue(53);

    return ratios;
}

NodeConfig CreateNodeConfig()
{
    NodeConfig nodeConfig;

    nodeConfig.mVersion  = "1.0.0";
    nodeConfig.mPriority = 1;
    nodeConfig.mNodeType = "mainType";

    nodeConfig.mLabels.PushBack("mainNode");

    nodeConfig.mAlertRules.SetValue(CreateAlerts());

    nodeConfig.mResourceRatios.SetValue(CreateResourceRatios());

    return nodeConfig;
}

void CompareNodeConfig(const NodeConfig& nodeConfig, const NodeConfig& expectedNodeConfig)
{
    EXPECT_EQ(nodeConfig.mVersion, expectedNodeConfig.mVersion) << "Version mismatch";
    EXPECT_EQ(nodeConfig.mNodeType, expectedNodeConfig.mNodeType) << "Node type mismatch";
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
class JSONProviderTest : public Test {
public:
    void SetUp() override { tests::utils::InitLog(); }

    JSONProvider mProvider;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(JSONProviderTest, NodeConfigFromJSONSucceeds)
{
    auto parsedNodeConfig = std::make_unique<NodeConfig>();

    ASSERT_EQ(mProvider.NodeConfigFromJSON(cTestNodeConfigJSON, *parsedNodeConfig), ErrorEnum::eNone);

    CompareNodeConfig(*parsedNodeConfig, CreateNodeConfig());
}

TEST_F(JSONProviderTest, NodeConfigFromJSONFailsOnLabelsExceedsLimit)
{
    auto parsedNodeConfig = std::make_unique<NodeConfig>();

    parsedNodeConfig->mLabels.Resize(cMaxNumNodeLabels);

    ASSERT_EQ(mProvider.NodeConfigFromJSON(cTestNodeConfigJSON, *parsedNodeConfig), ErrorEnum::eNoMemory);

    parsedNodeConfig = std::make_unique<NodeConfig>();

    ASSERT_EQ(mProvider.NodeConfigFromJSON(cNodeConfigLabelOverflowBuffer, *parsedNodeConfig), ErrorEnum::eNoMemory);
}

TEST_F(JSONProviderTest, NodeConfigToJSON)
{
    const NodeConfig nodeConfig       = CreateNodeConfig();
    auto             nodeConfigJSON   = std::make_unique<StaticString<nodeconfig::cNodeConfigJSONLen>>();
    auto             parsedNodeConfig = std::make_unique<NodeConfig>();

    ASSERT_EQ(mProvider.NodeConfigToJSON(nodeConfig, *nodeConfigJSON), ErrorEnum::eNone);

    ASSERT_EQ(mProvider.NodeConfigFromJSON(cTestNodeConfigJSON, *parsedNodeConfig), ErrorEnum::eNone);

    CompareNodeConfig(*parsedNodeConfig, nodeConfig);
}

} // namespace aos::common::jsonprovider
