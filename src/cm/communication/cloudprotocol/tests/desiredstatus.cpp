/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>

#include <cm/communication/cloudprotocol/desiredstatus.hpp>

using namespace testing;

namespace aos::cm::communication::cloudprotocol {

namespace {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

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

TEST_F(CloudProtocolDesiredStatus, Nodes)
{
    constexpr auto cJSON = R"({
        "messageType": "desiredStatus",
        "correlationID": "id",
        "nodes": [
            {
                "item": {"id": "node-1"},
                "state": "provisioned"
            },
            {
                "item": {"id": "node-2"},
                "state": "paused"
            }
        ]
    })";

    auto desiredStatus = std::make_unique<DesiredStatus>();

    auto [jsonVar, err] = common::utils::ParseJson(cJSON);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    auto wrapper = common::utils::CaseInsensitiveObjectWrapper(jsonVar);

    err = FromJSON(wrapper, *desiredStatus);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_STREQ(desiredStatus->mCorrelationID.CStr(), "id");
    ASSERT_EQ(desiredStatus->mNodes.Size(), 2);

    EXPECT_STREQ(desiredStatus->mNodes[0].mNodeID.CStr(), "node-1");
    EXPECT_EQ(desiredStatus->mNodes[0].mState, DesiredNodeStateEnum::eProvisioned);

    EXPECT_STREQ(desiredStatus->mNodes[1].mNodeID.CStr(), "node-2");
    EXPECT_EQ(desiredStatus->mNodes[1].mState, DesiredNodeStateEnum::ePaused);
}

TEST_F(CloudProtocolDesiredStatus, UnitConfig)
{
    constexpr auto cJSON = R"({
        "messageType": "desiredStatus",
        "correlationID": "id",
        "unitConfig": {
            "version": "v1.0.0",
            "formatVersion": "1.0",
            "nodes": [
                {
                    "nodeGroupSubject": {
                        "id": "main"
                    },
                    "node": {
                        "id": "node-1"
                    },
                    "alertRules": {
                        "cpu": {
                            "minThreshold": 10,
                            "maxThreshold": 90,
                            "minTimeout": 10
                        },
                        "ram": {
                            "minThreshold": 20,
                            "maxThreshold": 80,
                            "minTimeout": 5
                        },
                        "partitions": [
                            {
                                "minThreshold": 15,
                                "maxThreshold": 85,
                                "minTimeout": 5,
                                "name": "state"
                            },
                            {
                                "minThreshold": 25,
                                "maxThreshold": 75,
                                "minTimeout": 5,
                                "name": "storage"
                            }
                        ],
                        "download": {
                            "minThreshold": 100,
                            "maxThreshold": 100000,
                            "minTimeout": 10
                        },
                        "upload": {
                            "minThreshold": 50,
                            "maxThreshold": 50000,
                            "minTimeout": 5
                        }
                    },
                    "resourceRatios": {
                        "cpu": 10,
                        "ram": 20,
                        "storage": 30,
                        "state": 40
                    },
                    "labels": [
                        "label1",
                        "label2"
                    ],
                    "priority": 10
                },
                {
                    "nodeGroupSubject": {
                        "id": "secondary"
                    },
                    "node": {
                        "id": "node-2"
                    },
                    "priority": 20
                }
            ]
        }
    })";

    auto desiredStatus = std::make_unique<DesiredStatus>();

    auto [jsonVar, err] = common::utils::ParseJson(cJSON);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    auto wrapper = common::utils::CaseInsensitiveObjectWrapper(jsonVar);

    err = FromJSON(wrapper, *desiredStatus);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    ASSERT_TRUE(desiredStatus->mUnitConfig.HasValue());
    ASSERT_EQ(desiredStatus->mUnitConfig->mNodes.Size(), 2);

    EXPECT_EQ(desiredStatus->mUnitConfig->mVersion, "v1.0.0");
    EXPECT_EQ(desiredStatus->mUnitConfig->mFormatVersion, "1.0");

    const auto& node0 = desiredStatus->mUnitConfig->mNodes[0];
    EXPECT_STREQ(node0.mNodeType.CStr(), "main");
    EXPECT_STREQ(node0.mNodeID.CStr(), "node-1");

    ASSERT_TRUE(node0.mAlertRules.HasValue());

    ASSERT_TRUE(node0.mAlertRules->mCPU.HasValue());
    EXPECT_EQ(node0.mAlertRules->mCPU->mMinThreshold, 10);
    EXPECT_EQ(node0.mAlertRules->mCPU->mMaxThreshold, 90);
    EXPECT_EQ(node0.mAlertRules->mCPU->mMinTimeout.Seconds(), 10);

    ASSERT_TRUE(node0.mAlertRules->mRAM.HasValue());
    EXPECT_EQ(node0.mAlertRules->mRAM->mMinThreshold, 20);
    EXPECT_EQ(node0.mAlertRules->mRAM->mMaxThreshold, 80);
    EXPECT_EQ(node0.mAlertRules->mRAM->mMinTimeout.Seconds(), 5);

    ASSERT_EQ(node0.mAlertRules->mPartitions.Size(), 2);
    EXPECT_EQ(node0.mAlertRules->mPartitions[0].mMinThreshold, 15);
    EXPECT_EQ(node0.mAlertRules->mPartitions[0].mMaxThreshold, 85);
    EXPECT_EQ(node0.mAlertRules->mPartitions[0].mMinTimeout.Seconds(), 5);
    EXPECT_STREQ(node0.mAlertRules->mPartitions[0].mName.CStr(), "state");

    EXPECT_EQ(node0.mAlertRules->mPartitions[1].mMinThreshold, 25);
    EXPECT_EQ(node0.mAlertRules->mPartitions[1].mMaxThreshold, 75);
    EXPECT_EQ(node0.mAlertRules->mPartitions[1].mMinTimeout.Seconds(), 5);
    EXPECT_STREQ(node0.mAlertRules->mPartitions[1].mName.CStr(), "storage");

    ASSERT_TRUE(node0.mAlertRules->mDownload.HasValue());
    EXPECT_EQ(node0.mAlertRules->mDownload->mMinThreshold, 100);
    EXPECT_EQ(node0.mAlertRules->mDownload->mMaxThreshold, 100000);
    EXPECT_EQ(node0.mAlertRules->mDownload->mMinTimeout.Seconds(), 10);

    ASSERT_TRUE(node0.mAlertRules->mUpload.HasValue());
    EXPECT_EQ(node0.mAlertRules->mUpload->mMinThreshold, 50);
    EXPECT_EQ(node0.mAlertRules->mUpload->mMaxThreshold, 50000);
    EXPECT_EQ(node0.mAlertRules->mUpload->mMinTimeout.Seconds(), 5);

    ASSERT_TRUE(node0.mResourceRatios.HasValue());
    EXPECT_EQ(node0.mResourceRatios->mCPU, 10);
    EXPECT_EQ(node0.mResourceRatios->mRAM, 20);
    EXPECT_EQ(node0.mResourceRatios->mStorage, 30);
    EXPECT_EQ(node0.mResourceRatios->mState, 40);

    ASSERT_EQ(node0.mLabels.Size(), 2);
    EXPECT_STREQ(node0.mLabels[0].CStr(), "label1");
    EXPECT_STREQ(node0.mLabels[1].CStr(), "label2");

    EXPECT_EQ(node0.mPriority, 10);

    const auto& node1 = desiredStatus->mUnitConfig->mNodes[1];
    EXPECT_STREQ(node1.mNodeType.CStr(), "secondary");
    EXPECT_STREQ(node1.mNodeID.CStr(), "node-2");

    EXPECT_FALSE(node1.mAlertRules.HasValue());
    EXPECT_FALSE(node1.mResourceRatios.HasValue());
    EXPECT_EQ(node1.mLabels.Size(), 0);
    EXPECT_EQ(node1.mPriority, 20);
}

TEST_F(CloudProtocolDesiredStatus, Items)
{
    constexpr auto cJSON = R"({
        "messageType": "desiredStatus",
        "items": [
            {
                "item": {
                    "id": "item1"
                },
                "version": "0.0.1",
                "owner": {
                    "id": "owner1"
                },
                "indexDigest": "sha256:36f028580bb02cc8272a9a020f4200e346e276ae664e45ee80745574e2f5ab80"
            },
            {
                "item": {
                    "id": "item2"
                },
                "version": "1.2.3",
                "owner": {
                    "id": "owner2"
                },
                "indexDigest": "sha256:abcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd"
            }
        ]
    })";

    auto desiredStatus = std::make_unique<DesiredStatus>();

    auto [jsonVar, err] = common::utils::ParseJson(cJSON);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    auto wrapper = common::utils::CaseInsensitiveObjectWrapper(jsonVar);

    err = FromJSON(wrapper, *desiredStatus);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    ASSERT_EQ(desiredStatus->mUpdateItems.Size(), 2);

    const auto& item0 = desiredStatus->mUpdateItems[0];
    EXPECT_STREQ(item0.mItemID.CStr(), "item1");
    EXPECT_STREQ(item0.mOwnerID.CStr(), "owner1");
    EXPECT_STREQ(item0.mVersion.CStr(), "0.0.1");
    EXPECT_STREQ(item0.mIndexDigest.CStr(), "sha256:36f028580bb02cc8272a9a020f4200e346e276ae664e45ee80745574e2f5ab80");

    const auto& item1 = desiredStatus->mUpdateItems[1];
    EXPECT_STREQ(item1.mItemID.CStr(), "item2");
    EXPECT_STREQ(item1.mOwnerID.CStr(), "owner2");
    EXPECT_STREQ(item1.mVersion.CStr(), "1.2.3");
    EXPECT_STREQ(item1.mIndexDigest.CStr(), "sha256:abcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd");
}

TEST_F(CloudProtocolDesiredStatus, Instances)
{
    constexpr auto cJSON = R"({
        "messageType": "desiredStatus",
        "instances": [
            {
                "item": {
                    "id": "itemId1"
                },
                "subject": {
                    "id": "subjectId1"
                },
                "priority": 1,
                "numInstances": 2,
                "labels": [
                    "main"
                ]
            },
            {
                "item": {
                    "id": "itemId2"
                },
                "subject": {
                    "id": "subjectId2"
                }
            }
        ]
    })";

    auto desiredStatus = std::make_unique<DesiredStatus>();

    auto [jsonVar, err] = common::utils::ParseJson(cJSON);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    auto wrapper = common::utils::CaseInsensitiveObjectWrapper(jsonVar);

    err = FromJSON(wrapper, *desiredStatus);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    ASSERT_EQ(desiredStatus->mInstances.Size(), 2);

    const auto& instance0 = desiredStatus->mInstances[0];
    EXPECT_STREQ(instance0.mItemID.CStr(), "itemId1");
    EXPECT_STREQ(instance0.mSubjectID.CStr(), "subjectId1");
    EXPECT_EQ(instance0.mPriority, 1);
    EXPECT_EQ(instance0.mNumInstances, 2);
    ASSERT_EQ(instance0.mLabels.Size(), 1);
    EXPECT_STREQ(instance0.mLabels[0].CStr(), "main");

    const auto& instance1 = desiredStatus->mInstances[1];
    EXPECT_STREQ(instance1.mItemID.CStr(), "itemId2");
    EXPECT_STREQ(instance1.mSubjectID.CStr(), "subjectId2");
    EXPECT_EQ(instance1.mPriority, 0);
    EXPECT_EQ(instance1.mNumInstances, 0);
    ASSERT_EQ(instance1.mLabels.Size(), 0);
}

TEST_F(CloudProtocolDesiredStatus, Subjects)
{
    constexpr auto cJSON = R"({
        "messageType": "desiredStatus",
        "subjects": [
            {
                "identity": {
                    "id": "subjectId1"
                },
                "type": "group"
            },
            {
                "identity": {
                    "id": "subjectId2"
                },
                "type": "user"
            }
        ]
    })";

    auto desiredStatus = std::make_unique<DesiredStatus>();

    auto [jsonVar, err] = common::utils::ParseJson(cJSON);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    auto wrapper = common::utils::CaseInsensitiveObjectWrapper(jsonVar);

    err = FromJSON(wrapper, *desiredStatus);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    ASSERT_EQ(desiredStatus->mSubjects.Size(), 2);

    EXPECT_STREQ(desiredStatus->mSubjects[0].mSubjectID.CStr(), "subjectId1");
    EXPECT_EQ(desiredStatus->mSubjects[0].mSubjectType.GetValue(), SubjectTypeEnum::eGroup);

    EXPECT_STREQ(desiredStatus->mSubjects[1].mSubjectID.CStr(), "subjectId2");
    EXPECT_EQ(desiredStatus->mSubjects[1].mSubjectType.GetValue(), SubjectTypeEnum::eUser);
}

TEST_F(CloudProtocolDesiredStatus, Certificates)
{
    constexpr auto cJSON = R"({
        "messageType": "desiredStatus",
        "certificates": [
            {
                "certificate": "ZGVyIGNlcnRpZmljYXRlIGV4YW1wbGU=",
                "fingerprint": "fingerprint"
            }
        ]
    })";

    auto desiredStatus = std::make_unique<DesiredStatus>();

    auto [jsonVar, err] = common::utils::ParseJson(cJSON);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    auto wrapper = common::utils::CaseInsensitiveObjectWrapper(jsonVar);

    err = FromJSON(wrapper, *desiredStatus);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    ASSERT_EQ(desiredStatus->mCertificates.Size(), 1);
    EXPECT_EQ(desiredStatus->mCertificates[0].mCertificate, String("der certificate example").AsByteArray());
    EXPECT_STREQ(desiredStatus->mCertificates[0].mFingerprint.CStr(), "fingerprint");
}

TEST_F(CloudProtocolDesiredStatus, CertificateChains)
{
    constexpr auto cJSON = R"({
        "messageType": "desiredStatus",
        "certificateChains": [
            {
                "name": "name1",
                "fingerprints": [
                    "fingerprint1",
                    "fingerprint2"
                ]
            },
            {
                "name": "name2",
                "fingerprints": [
                    "fingerprint3"
                ]
            }
        ]
    })";

    auto desiredStatus = std::make_unique<DesiredStatus>();

    auto [jsonVar, err] = common::utils::ParseJson(cJSON);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    auto wrapper = common::utils::CaseInsensitiveObjectWrapper(jsonVar);

    err = FromJSON(wrapper, *desiredStatus);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    ASSERT_EQ(desiredStatus->mCertificateChains.Size(), 2);

    EXPECT_STREQ(desiredStatus->mCertificateChains[0].mName.CStr(), "name1");
    ASSERT_EQ(desiredStatus->mCertificateChains[0].mFingerprints.Size(), 2);
    EXPECT_STREQ(desiredStatus->mCertificateChains[0].mFingerprints[0].CStr(), "fingerprint1");
    EXPECT_STREQ(desiredStatus->mCertificateChains[0].mFingerprints[1].CStr(), "fingerprint2");

    EXPECT_STREQ(desiredStatus->mCertificateChains[1].mName.CStr(), "name2");
    ASSERT_EQ(desiredStatus->mCertificateChains[1].mFingerprints.Size(), 1);
    EXPECT_STREQ(desiredStatus->mCertificateChains[1].mFingerprints[0].CStr(), "fingerprint3");
}

} // namespace aos::cm::communication::cloudprotocol
