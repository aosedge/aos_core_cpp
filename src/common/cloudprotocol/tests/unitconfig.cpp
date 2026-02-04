/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>

#include <common/cloudprotocol/unitconfig.hpp>
#include <common/utils/json.hpp>

using namespace testing;

namespace aos::common::cloudprotocol {

namespace {

/***********************************************************************************************************************
 * Consts
 **********************************************************************************************************************/

constexpr auto cTestNodeConfigJSON = R"({
    "version": "1.0.0",
    "node": {
        "codename": "node-id"
    },
    "nodeGroupSubject": {
        "codename": "mainType"
    },
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
    "labels": [
        "mainNode"
    ],
    "priority": 1
}
)";

constexpr auto cNodeConfigLabelOverflowBuffer = R"({
    "version": "1.0.0",
    "node": {
        "codename": "node-id"
    },
    "nodeGroupSubject": {
        "codename": "mainType"
    },
    "labels": [
        "label that is expected to trigger no memory error due to its length"
    ],
    "priority": 1
}
)";

constexpr auto cTestUnitConfigJSON = R"({
    "version": "2.0.0",
    "formatVersion": "7",
    "nodes": [
        {
            "version": "1.0.0",
            "node": {
                "codename": "node-1"
            },
            "nodeGroupSubject": {
                "codename": "mainType"
            },
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
            "labels": [
                "mainNode"
            ],
            "priority": 1
        },
        {
            "version": "1.0.0",
            "node": {
                "codename": "node-2"
            },
            "nodeGroupSubject": {
                "codename": "secondaryType"
            },
            "labels": [
                "secondaryNode"
            ],
            "priority": 2
        }
    ]
})";

constexpr auto cTestUnitConfigEmptyNodesJSON = R"({
    "version": "1.0.0",
    "formatVersion": "7"
})";

constexpr auto cTestUnitConfigMinimalJSON = R"({
    "version": "1.0.0",
    "formatVersion": "7",
    "nodes": [
        {
            "version": "1.0.0",
            "node": {
                "codename": "node-1"
            },
            "nodeGroupSubject": {
                "codename": "type1"
            },
            "priority": 0
        }
    ]
})";

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

    nodeConfig.mNodeID   = "node-id";
    nodeConfig.mNodeType = "mainType";
    nodeConfig.mVersion  = "1.0.0";
    nodeConfig.mAlertRules.SetValue(CreateAlerts());
    nodeConfig.mResourceRatios.SetValue(CreateResourceRatios());
    nodeConfig.mLabels.PushBack("mainNode");
    nodeConfig.mPriority = 1;

    return nodeConfig;
}

void CompareNodeConfig(const NodeConfig& nodeConfig, const NodeConfig& expectedNodeConfig)
{
    EXPECT_EQ(nodeConfig.mNodeID, expectedNodeConfig.mNodeID) << "Node ID mismatch";
    EXPECT_EQ(nodeConfig.mVersion, expectedNodeConfig.mVersion) << "Version mismatch";
    EXPECT_EQ(nodeConfig.mNodeType, expectedNodeConfig.mNodeType) << "Node type mismatch";
    EXPECT_EQ(nodeConfig.mPriority, expectedNodeConfig.mPriority) << "Priority mismatch";
    EXPECT_EQ(nodeConfig.mLabels, expectedNodeConfig.mLabels) << "Node labels mismatch";

    // Compare alert rules
    EXPECT_EQ(nodeConfig.mAlertRules.HasValue(), expectedNodeConfig.mAlertRules.HasValue())
        << "Alert rules presence mismatch";

    if (nodeConfig.mAlertRules.HasValue() && expectedNodeConfig.mAlertRules.HasValue()) {
        EXPECT_EQ(nodeConfig.mAlertRules->mRAM, expectedNodeConfig.mAlertRules->mRAM) << "Alert rules ram mismatch";
        EXPECT_EQ(nodeConfig.mAlertRules->mCPU, expectedNodeConfig.mAlertRules->mCPU) << "Alert rules cpu mismatch";
        EXPECT_EQ(nodeConfig.mAlertRules->mPartitions, expectedNodeConfig.mAlertRules->mPartitions)
            << "Alert rules partitions mismatch";
        EXPECT_EQ(nodeConfig.mAlertRules->mDownload, expectedNodeConfig.mAlertRules->mDownload)
            << "Alert rules download mismatch";
        EXPECT_EQ(nodeConfig.mAlertRules->mUpload, expectedNodeConfig.mAlertRules->mUpload)
            << "Alert rules upload mismatch";
    }

    EXPECT_EQ(nodeConfig.mResourceRatios.HasValue(), expectedNodeConfig.mResourceRatios.HasValue())
        << "Resource ratios presence mismatch";

    if (nodeConfig.mResourceRatios.HasValue() && expectedNodeConfig.mResourceRatios.HasValue()) {
        EXPECT_EQ(nodeConfig.mResourceRatios->mCPU, expectedNodeConfig.mResourceRatios->mCPU)
            << "Resource ratios cpu mismatch";
        EXPECT_EQ(nodeConfig.mResourceRatios->mRAM, expectedNodeConfig.mResourceRatios->mRAM)
            << "Resource ratios ram mismatch";
        EXPECT_EQ(nodeConfig.mResourceRatios->mStorage, expectedNodeConfig.mResourceRatios->mStorage)
            << "Resource ratios storage mismatch";
        EXPECT_EQ(nodeConfig.mResourceRatios->mState, expectedNodeConfig.mResourceRatios->mState)
            << "Resource ratios state mismatch";
    }
}

UnitConfig CreateUnitConfig()
{
    UnitConfig unitConfig;

    unitConfig.mVersion       = "2.0.0";
    unitConfig.mFormatVersion = "7";

    NodeConfig node1;
    node1.mNodeID   = "node-1";
    node1.mNodeType = "mainType";
    node1.mVersion  = "1.0.0";
    node1.mAlertRules.SetValue(CreateAlerts());
    node1.mResourceRatios.SetValue(CreateResourceRatios());
    node1.mLabels.PushBack("mainNode");
    node1.mPriority = 1;
    unitConfig.mNodes.PushBack(node1);

    NodeConfig node2;
    node2.mNodeID   = "node-2";
    node2.mNodeType = "secondaryType";
    node2.mVersion  = "1.0.0";
    node2.mLabels.PushBack("secondaryNode");
    node2.mPriority = 2;
    unitConfig.mNodes.PushBack(node2);

    return unitConfig;
}

void CompareUnitConfig(const UnitConfig& unitConfig, const UnitConfig& expectedUnitConfig)
{
    EXPECT_EQ(unitConfig.mVersion, expectedUnitConfig.mVersion) << "Unit config version mismatch";
    EXPECT_EQ(unitConfig.mFormatVersion, expectedUnitConfig.mFormatVersion) << "Unit config format version mismatch";
    ASSERT_EQ(unitConfig.mNodes.Size(), expectedUnitConfig.mNodes.Size()) << "Nodes count mismatch";

    for (size_t i = 0; i < unitConfig.mNodes.Size(); ++i) {
        CompareNodeConfig(unitConfig.mNodes[i], expectedUnitConfig.mNodes[i]);
    }
}

} // namespace

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class CloudProtocolUnitConfig : public Test {
public:
    static void SetUpTestSuite() { tests::utils::InitLog(); }
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(CloudProtocolUnitConfig, NodeConfigFromJSONSucceeds)
{
    auto parsedNodeConfig = std::make_unique<NodeConfig>();

    auto [json, err] = common::utils::ParseJson(cTestNodeConfigJSON);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = FromJSON(common::utils::CaseInsensitiveObjectWrapper(json), *parsedNodeConfig);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    CompareNodeConfig(*parsedNodeConfig, CreateNodeConfig());
}

TEST_F(CloudProtocolUnitConfig, NodeConfigFromJSONFailsOnLabelsExceedsLimit)
{
    auto parsedNodeConfig = std::make_unique<NodeConfig>();

    parsedNodeConfig->mLabels.Resize(cMaxNumNodeLabels);

    auto [json, err] = common::utils::ParseJson(cNodeConfigLabelOverflowBuffer);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    auto wrapper = common::utils::CaseInsensitiveObjectWrapper(json);

    err = FromJSON(wrapper, *parsedNodeConfig);
    ASSERT_TRUE(err.Is(ErrorEnum::eNoMemory)) << "Expected no memory error but got: " << tests::utils::ErrorToStr(err);

    parsedNodeConfig = std::make_unique<NodeConfig>();

    err = FromJSON(wrapper, *parsedNodeConfig);
    ASSERT_TRUE(err.Is(ErrorEnum::eNoMemory)) << "Expected no memory error but got: " << tests::utils::ErrorToStr(err);
}

TEST_F(CloudProtocolUnitConfig, NodeConfigToJSON)
{
    const NodeConfig nodeConfig       = CreateNodeConfig();
    auto             parsedNodeConfig = std::make_unique<NodeConfig>();
    auto             json             = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(nodeConfig, *json);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = FromJSON(common::utils::CaseInsensitiveObjectWrapper(json), *parsedNodeConfig);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    CompareNodeConfig(*parsedNodeConfig, nodeConfig);
}

TEST_F(CloudProtocolUnitConfig, UnitConfigFromJSONSucceeds)
{
    auto parsedUnitConfig = std::make_unique<UnitConfig>();

    auto [json, err] = common::utils::ParseJson(cTestUnitConfigJSON);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = FromJSON(common::utils::CaseInsensitiveObjectWrapper(json), *parsedUnitConfig);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    CompareUnitConfig(*parsedUnitConfig, CreateUnitConfig());
}

TEST_F(CloudProtocolUnitConfig, UnitConfigFromJSONEmptyNodes)
{
    auto parsedUnitConfig = std::make_unique<UnitConfig>();

    auto [json, err] = common::utils::ParseJson(cTestUnitConfigEmptyNodesJSON);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = FromJSON(common::utils::CaseInsensitiveObjectWrapper(json), *parsedUnitConfig);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(parsedUnitConfig->mVersion, "1.0.0");
    EXPECT_EQ(parsedUnitConfig->mFormatVersion, "7");
    EXPECT_TRUE(parsedUnitConfig->mNodes.IsEmpty());
}

TEST_F(CloudProtocolUnitConfig, UnitConfigFromJSONMinimal)
{
    auto parsedUnitConfig = std::make_unique<UnitConfig>();

    auto [json, err] = common::utils::ParseJson(cTestUnitConfigMinimalJSON);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = FromJSON(common::utils::CaseInsensitiveObjectWrapper(json), *parsedUnitConfig);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(parsedUnitConfig->mVersion, "1.0.0");
    EXPECT_EQ(parsedUnitConfig->mFormatVersion, "7");
    ASSERT_EQ(parsedUnitConfig->mNodes.Size(), 1);
    EXPECT_EQ(parsedUnitConfig->mNodes[0].mNodeID, "node-1");
    EXPECT_EQ(parsedUnitConfig->mNodes[0].mNodeType, "type1");
    EXPECT_EQ(parsedUnitConfig->mNodes[0].mPriority, 0);
    EXPECT_FALSE(parsedUnitConfig->mNodes[0].mAlertRules.HasValue());
    EXPECT_FALSE(parsedUnitConfig->mNodes[0].mResourceRatios.HasValue());
}

TEST_F(CloudProtocolUnitConfig, UnitConfigToJSON)
{
    const auto unitConfig       = CreateUnitConfig();
    auto       parsedUnitConfig = std::make_unique<UnitConfig>();
    auto       json             = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(unitConfig, *json);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = FromJSON(common::utils::CaseInsensitiveObjectWrapper(json), *parsedUnitConfig);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    CompareUnitConfig(*parsedUnitConfig, unitConfig);
}

TEST_F(CloudProtocolUnitConfig, UnitConfigRoundTrip)
{
    auto json           = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);
    auto originalConfig = std::make_unique<UnitConfig>();

    originalConfig->mVersion       = "3.0.0";
    originalConfig->mFormatVersion = "7";

    auto nodeConfig = std::make_unique<NodeConfig>();

    nodeConfig->mNodeID   = "test-node";
    nodeConfig->mNodeType = "testType";
    nodeConfig->mVersion  = "2.0.0";
    nodeConfig->mPriority = 5;
    nodeConfig->mLabels.PushBack("label1");
    nodeConfig->mLabels.PushBack("label2");

    originalConfig->mNodes.PushBack(*nodeConfig);

    auto parsedConfig = std::make_unique<UnitConfig>();

    auto err = ToJSON(*originalConfig, *json);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    auto jsonStr = common::utils::Stringify(json);

    Poco::Dynamic::Var jsonVar;

    Tie(jsonVar, err) = common::utils::ParseJson(jsonStr);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = FromJSON(common::utils::CaseInsensitiveObjectWrapper(jsonVar), *parsedConfig);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    CompareUnitConfig(*parsedConfig, *originalConfig);
}

} // namespace aos::common::cloudprotocol
