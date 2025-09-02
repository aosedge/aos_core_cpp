/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>

#include <common/cloudprotocol/unitstatus.hpp>
#include <common/utils/json.hpp>

using namespace testing;

namespace aos::common::cloudprotocol {

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class CloudProtocolUnitStatus : public Test {
public:
    void SetUp() override { tests::utils::InitLog(); }
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(CloudProtocolUnitStatus, EmptyUnitStatus)
{
    auto status = std::make_unique<aos::cloudprotocol::UnitStatus>();

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(*status, *json);
    ASSERT_TRUE(err.IsNone()) << "Error: " << tests::utils::ErrorToStr(err);

    auto jsonWrapper = utils::CaseInsensitiveObjectWrapper(json);

    EXPECT_FALSE(jsonWrapper.GetValue<bool>("isDeltaInfo", false));

    EXPECT_TRUE(jsonWrapper.Has("unitConfig"));
    EXPECT_TRUE(jsonWrapper.Has("nodes"));
    EXPECT_TRUE(jsonWrapper.Has("services"));
    EXPECT_TRUE(jsonWrapper.Has("instances"));
    EXPECT_TRUE(jsonWrapper.Has("layers"));
    EXPECT_TRUE(jsonWrapper.Has("components"));
    EXPECT_TRUE(jsonWrapper.Has("unitSubjects"));
}

TEST_F(CloudProtocolUnitStatus, UnitConfigStatus)
{
    auto status = std::make_unique<aos::cloudprotocol::UnitStatus>();

    status->mUnitConfigStatus.PushBack({"v1.0.0", ItemStatusEnum::eRemoved, ErrorEnum::eNone});
    status->mUnitConfigStatus.PushBack({"v1.0.1", ItemStatusEnum::eError, ErrorEnum::eFailed});
    status->mUnitConfigStatus.PushBack({"v1.0.2", ItemStatusEnum::eInstalled, ErrorEnum::eNone});

    status->mNodeInfo.EmplaceBack();

    status->mNodeInfo[0].mNodeID   = "node1";
    status->mNodeInfo[0].mNodeType = "type1";
    status->mNodeInfo[0].mName     = "Node 1";
    status->mNodeInfo[0].mStatus   = NodeStateEnum::eProvisioned;
    status->mNodeInfo[0].mOSType   = "Linux";
    status->mNodeInfo[0].mCPUs.PushBack({"cpu1", 4, 8, "arch1", {}, 2000});
    status->mNodeInfo[0].mCPUs.PushBack({"cpu2", 4, 8, "arch2", {"arch f2"}, 4000});
    status->mNodeInfo[0].mTotalRAM = 16000;
    status->mNodeInfo[0].mMaxDMIPS = 8000;

    status->mNodeInfo[0].mPartitions.PushBack({"partition1", {}, "", 5000, 0});
    status->mNodeInfo[0].mPartitions.PushBack({"partition2", {}, "", 10000, 0});
    status->mNodeInfo[0].mPartitions[1].mTypes.PushBack("type1");
    status->mNodeInfo[0].mPartitions[1].mTypes.PushBack("type2");

    status->mNodeInfo[0].mAttrs.PushBack({"attr1", "value1"});
    status->mNodeInfo[0].mAttrs.PushBack({"attr2", "value2"});

    status->mNodeInfo.EmplaceBack();
    status->mNodeInfo[1].mNodeID = "node2";

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(*status, *json);
    ASSERT_TRUE(err.IsNone()) << "Error: " << tests::utils::ErrorToStr(err);

    auto jsonWrapper = utils::CaseInsensitiveObjectWrapper(json);

    EXPECT_FALSE(jsonWrapper.GetValue<bool>("isDeltaInfo", false));

    EXPECT_TRUE(jsonWrapper.Has("nodes"));
    EXPECT_TRUE(jsonWrapper.Has("services"));
    EXPECT_TRUE(jsonWrapper.Has("instances"));
    EXPECT_TRUE(jsonWrapper.Has("layers"));
    EXPECT_TRUE(jsonWrapper.Has("components"));
    EXPECT_TRUE(jsonWrapper.Has("unitSubjects"));

    auto unparsedStatus = std::make_unique<aos::cloudprotocol::UnitStatus>();

    err = FromJSON(jsonWrapper, *unparsedStatus);
    ASSERT_TRUE(err.IsNone()) << "Failed to parse JSON: " << tests::utils::ErrorToStr(err);

    EXPECT_EQ(*status, *unparsedStatus);
}

TEST_F(CloudProtocolUnitStatus, EmptyDeltaUnitStatus)
{
    auto status = std::make_unique<aos::cloudprotocol::DeltaUnitStatus>();

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    auto err = ToJSON(*status, *json);
    ASSERT_TRUE(err.IsNone()) << "Error: " << tests::utils::ErrorToStr(err);

    auto jsonWrapper = utils::CaseInsensitiveObjectWrapper(json);

    EXPECT_TRUE(jsonWrapper.GetValue<bool>("isDeltaInfo", false));

    EXPECT_FALSE(jsonWrapper.Has("unitConfig"));
    EXPECT_FALSE(jsonWrapper.Has("nodes"));
    EXPECT_FALSE(jsonWrapper.Has("services"));
    EXPECT_FALSE(jsonWrapper.Has("instances"));
    EXPECT_FALSE(jsonWrapper.Has("layers"));
    EXPECT_FALSE(jsonWrapper.Has("components"));
    EXPECT_FALSE(jsonWrapper.Has("unitSubjects"));
}

} // namespace aos::common::cloudprotocol
