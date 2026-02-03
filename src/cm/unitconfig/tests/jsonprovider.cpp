/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <memory>

#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>

#include <cm/unitconfig/jsonprovider.hpp>

using namespace testing;

namespace aos::cm::unitconfig {

namespace {

/***********************************************************************************************************************
 * Consts
 **********************************************************************************************************************/

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
    "formatVersion": "7",
    "nodes": []
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

class JSONProviderTest : public Test {
public:
    static void SetUpTestSuite() { tests::utils::InitLog(); }

    JSONProvider mProvider;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(JSONProviderTest, UnitConfigFromJSONSucceeds)
{
    UnitConfig parsedUnitConfig;

    ASSERT_EQ(mProvider.UnitConfigFromJSON(cTestUnitConfigJSON, parsedUnitConfig), ErrorEnum::eNone);

    CompareUnitConfig(parsedUnitConfig, CreateUnitConfig());
}

TEST_F(JSONProviderTest, UnitConfigFromJSONEmptyNodes)
{
    UnitConfig parsedUnitConfig;

    ASSERT_EQ(mProvider.UnitConfigFromJSON(cTestUnitConfigEmptyNodesJSON, parsedUnitConfig), ErrorEnum::eNone);

    EXPECT_EQ(parsedUnitConfig.mVersion, "1.0.0");
    EXPECT_EQ(parsedUnitConfig.mFormatVersion, "7");
    EXPECT_TRUE(parsedUnitConfig.mNodes.IsEmpty());
}

TEST_F(JSONProviderTest, UnitConfigFromJSONMinimal)
{
    UnitConfig parsedUnitConfig;

    ASSERT_EQ(mProvider.UnitConfigFromJSON(cTestUnitConfigMinimalJSON, parsedUnitConfig), ErrorEnum::eNone);

    EXPECT_EQ(parsedUnitConfig.mVersion, "1.0.0");
    EXPECT_EQ(parsedUnitConfig.mFormatVersion, "7");
    ASSERT_EQ(parsedUnitConfig.mNodes.Size(), 1);
    EXPECT_EQ(parsedUnitConfig.mNodes[0].mNodeID, "node-1");
    EXPECT_EQ(parsedUnitConfig.mNodes[0].mNodeType, "type1");
    EXPECT_EQ(parsedUnitConfig.mNodes[0].mPriority, 0);
    EXPECT_FALSE(parsedUnitConfig.mNodes[0].mAlertRules.HasValue());
    EXPECT_FALSE(parsedUnitConfig.mNodes[0].mResourceRatios.HasValue());
}

TEST_F(JSONProviderTest, UnitConfigToJSON)
{
    const UnitConfig unitConfig     = CreateUnitConfig();
    auto             unitConfigJSON = std::make_unique<StaticString<cJSONMaxLen>>();
    UnitConfig       parsedUnitConfig;

    ASSERT_EQ(mProvider.UnitConfigToJSON(unitConfig, *unitConfigJSON), ErrorEnum::eNone);
    ASSERT_EQ(mProvider.UnitConfigFromJSON(*unitConfigJSON, parsedUnitConfig), ErrorEnum::eNone);

    CompareUnitConfig(parsedUnitConfig, unitConfig);
}

TEST_F(JSONProviderTest, UnitConfigRoundTrip)
{
    UnitConfig originalConfig;
    originalConfig.mVersion       = "3.0.0";
    originalConfig.mFormatVersion = "7";

    NodeConfig node;
    node.mNodeID   = "test-node";
    node.mNodeType = "testType";
    node.mVersion  = "2.0.0";
    node.mPriority = 5;
    node.mLabels.PushBack("label1");
    node.mLabels.PushBack("label2");
    originalConfig.mNodes.PushBack(node);

    auto       json = std::make_unique<StaticString<cJSONMaxLen>>();
    UnitConfig parsedConfig;

    ASSERT_EQ(mProvider.UnitConfigToJSON(originalConfig, *json), ErrorEnum::eNone);
    ASSERT_EQ(mProvider.UnitConfigFromJSON(*json, parsedConfig), ErrorEnum::eNone);

    CompareUnitConfig(parsedConfig, originalConfig);
}

} // namespace aos::cm::unitconfig
