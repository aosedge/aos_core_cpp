/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <iostream>

#include <gmock/gmock.h>

#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>

#include <sm/database/database.hpp>

using namespace testing;

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

namespace {

aos::InstanceIdent CreateInstanceIdent(const std::string& itemID, const std::string& subjectID, uint32_t instance,
    aos::UpdateItemType type = aos::UpdateItemTypeEnum::eService)
{
    aos::InstanceIdent ident;

    ident.mItemID    = itemID.c_str();
    ident.mSubjectID = subjectID.c_str();
    ident.mInstance  = instance;
    ident.mType      = type;

    return ident;
}

aos::InstanceInfo CreateInstanceInfo(
    const std::string& itemID, const std::string& subjectID, uint32_t instance, uint32_t uid = 10)
{
    aos::InstanceInfo info;

    info.mItemID         = itemID.c_str();
    info.mSubjectID      = subjectID.c_str();
    info.mInstance       = instance;
    info.mType           = aos::UpdateItemTypeEnum::eService;
    info.mVersion        = "1.0.0";
    info.mManifestDigest = "sha256:digest123";
    info.mRuntimeID      = "runtime-1";
    info.mOwnerID        = "owner-1";
    info.mSubjectType    = aos::SubjectTypeEnum::eUser;
    info.mUID            = uid;
    info.mGID            = uid + 1;
    info.mPriority       = 20;
    info.mStoragePath    = "storage-path";
    info.mStatePath      = "state-path";

    return info;
}

aos::sm::imagemanager::UpdateItemData CreateUpdateItemData(const aos::String& id, aos::UpdateItemType type,
    const aos::String& version, const aos::String& manifestDigest, aos::ItemState state, aos::Time timestamp)
{
    aos::sm::imagemanager::UpdateItemData item;

    item.mID             = id;
    item.mType           = type;
    item.mVersion        = version;
    item.mManifestDigest = manifestDigest;
    item.mState          = state;
    item.mTimestamp      = timestamp;

    return item;
}

} // namespace

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class DatabaseTest : public Test {
protected:
    void SetUp() override
    {
        aos::tests::utils::InitLog();

        namespace fs = std::filesystem;

        // Use source directory for migration files (next to test source file)
        fs::path sourceDir = fs::path(__FILE__).parent_path();
        mWorkingDir        = sourceDir / "database_test";

        // Clean up on start (after mWorkingDir is set)
        fs::remove_all(mWorkingDir);
        fs::create_directories(mWorkingDir);

        // Point directly to source migration directory
        auto migrationDir = fs::canonical(sourceDir / ".." / "migration");

        mMigrationConfig.mMigrationPath       = migrationDir.string();
        mMigrationConfig.mMergedMigrationPath = (mWorkingDir / "merged-migration").string();
    }

    void TearDown() override { std::filesystem::remove_all(mWorkingDir); }

protected:
    std::filesystem::path          mWorkingDir;
    aos::common::config::Migration mMigrationConfig;
    aos::sm::database::Database    mDB;
};

/***********************************************************************************************************************
 * Tests - imagemanager::StorageItf
 **********************************************************************************************************************/

TEST_F(DatabaseTest, AddUpdateItem)
{
    ASSERT_TRUE(mDB.Init(mWorkingDir.string(), mMigrationConfig).IsNone());

    auto item1v1 = CreateUpdateItemData("item1", aos::UpdateItemTypeEnum::eService, "1.0.0", "sha256:digest1",
        aos::ItemStateEnum::eInstalled, aos::Time::Now());
    auto item1v2 = CreateUpdateItemData("item1", aos::UpdateItemTypeEnum::eService, "2.0.0", "sha256:digest2",
        aos::ItemStateEnum::eInstalled, aos::Time::Now());
    auto item2v1 = CreateUpdateItemData("item2", aos::UpdateItemTypeEnum::eService, "1.0.0", "sha256:digest3",
        aos::ItemStateEnum::eInstalled, aos::Time::Now());

    // Add item1 v1

    auto err = mDB.AddUpdateItem(item1v1);
    ASSERT_TRUE(err.IsNone()) << aos::tests::utils::ErrorToStr(err);

    // Try to add same item again

    err = mDB.AddUpdateItem(item1v1);
    ASSERT_FALSE(err.IsNone()) << aos::tests::utils::ErrorToStr(err);

    // Add item1 v2

    err = mDB.AddUpdateItem(item1v2);
    ASSERT_TRUE(err.IsNone()) << aos::tests::utils::ErrorToStr(err);

    // Add item2 v1

    err = mDB.AddUpdateItem(item2v1);
    ASSERT_TRUE(err.IsNone()) << aos::tests::utils::ErrorToStr(err);

    // Get item1

    auto itemData = std::make_unique<aos::StaticArray<aos::sm::imagemanager::UpdateItemData, 2>>();

    err = mDB.GetUpdateItem("item1", *itemData);
    ASSERT_TRUE(err.IsNone()) << aos::tests::utils::ErrorToStr(err);

    ASSERT_EQ(itemData->Size(), 2);
    EXPECT_EQ((*itemData)[0], item1v1);
    EXPECT_EQ((*itemData)[1], item1v2);

    // Get item2

    itemData->Clear();

    err = mDB.GetUpdateItem("item2", *itemData);
    ASSERT_TRUE(err.IsNone()) << aos::tests::utils::ErrorToStr(err);

    ASSERT_EQ(itemData->Size(), 1);
    EXPECT_EQ((*itemData)[0], item2v1);
}

TEST_F(DatabaseTest, UpdateUpdateItem)
{
    ASSERT_TRUE(mDB.Init(mWorkingDir.string(), mMigrationConfig).IsNone());

    auto item1 = CreateUpdateItemData("item1", aos::UpdateItemTypeEnum::eService, "1.0.0", "sha256:digest1",
        aos::ItemStateEnum::eInstalled, aos::Time::Now());
    auto item2 = CreateUpdateItemData("item2", aos::UpdateItemTypeEnum::eService, "1.0.0", "sha256:digest3",
        aos::ItemStateEnum::eInstalled, aos::Time::Now());

    // Add item1

    auto err = mDB.AddUpdateItem(item1);
    ASSERT_TRUE(err.IsNone()) << aos::tests::utils::ErrorToStr(err);

    // Add item2

    err = mDB.AddUpdateItem(item2);
    ASSERT_TRUE(err.IsNone()) << aos::tests::utils::ErrorToStr(err);

    item1.mState = aos::ItemStateEnum::eRemoved;
    item2.mState = aos::ItemStateEnum::eRemoved;

    // Update item1

    err = mDB.UpdateUpdateItem(item1);
    ASSERT_TRUE(err.IsNone()) << aos::tests::utils::ErrorToStr(err);

    // Update item2

    err = mDB.UpdateUpdateItem(item2);
    ASSERT_TRUE(err.IsNone()) << aos::tests::utils::ErrorToStr(err);

    // Get item1

    auto itemData = std::make_unique<aos::StaticArray<aos::sm::imagemanager::UpdateItemData, 2>>();

    err = mDB.GetUpdateItem("item1", *itemData);
    ASSERT_TRUE(err.IsNone()) << aos::tests::utils::ErrorToStr(err);

    ASSERT_EQ(itemData->Size(), 1);
    EXPECT_EQ((*itemData)[0], item1);

    // Get item2

    itemData->Clear();

    err = mDB.GetUpdateItem("item2", *itemData);
    ASSERT_TRUE(err.IsNone()) << aos::tests::utils::ErrorToStr(err);

    ASSERT_EQ(itemData->Size(), 1);
    EXPECT_EQ((*itemData)[0], item2);
}

TEST_F(DatabaseTest, RemoveUpdateItem)
{
    ASSERT_TRUE(mDB.Init(mWorkingDir.string(), mMigrationConfig).IsNone());

    auto item1v1 = CreateUpdateItemData("item1", aos::UpdateItemTypeEnum::eService, "1.0.0", "sha256:digest1",
        aos::ItemStateEnum::eInstalled, aos::Time::Now());
    auto item1v2 = CreateUpdateItemData("item1", aos::UpdateItemTypeEnum::eService, "2.0.0", "sha256:digest2",
        aos::ItemStateEnum::eInstalled, aos::Time::Now());
    auto item2v1 = CreateUpdateItemData("item2", aos::UpdateItemTypeEnum::eService, "1.0.0", "sha256:digest3",
        aos::ItemStateEnum::eInstalled, aos::Time::Now());

    // Add item 1 v1

    auto err = mDB.AddUpdateItem(item1v1);
    ASSERT_TRUE(err.IsNone()) << aos::tests::utils::ErrorToStr(err);

    // Add item 1 v2

    err = mDB.AddUpdateItem(item1v2);
    ASSERT_TRUE(err.IsNone()) << aos::tests::utils::ErrorToStr(err);

    // Add item 2 v1

    err = mDB.AddUpdateItem(item2v1);
    ASSERT_TRUE(err.IsNone()) << aos::tests::utils::ErrorToStr(err);

    // Remove item1 v1 with wrong version

    err = mDB.RemoveUpdateItem(item1v1.mID, "3.0.0");
    ASSERT_TRUE(err.Is(aos::ErrorEnum::eNotFound)) << aos::tests::utils::ErrorToStr(err);

    // Remove item1 v1

    err = mDB.RemoveUpdateItem(item1v1.mID, item1v1.mVersion);
    ASSERT_TRUE(err.IsNone()) << aos::tests::utils::ErrorToStr(err);

    // Remove item2 v1

    err = mDB.RemoveUpdateItem(item2v1.mID, item2v1.mVersion);
    ASSERT_TRUE(err.IsNone()) << aos::tests::utils::ErrorToStr(err);

    // Get item1

    auto itemData = std::make_unique<aos::StaticArray<aos::sm::imagemanager::UpdateItemData, 2>>();

    err = mDB.GetUpdateItem("item1", *itemData);
    ASSERT_TRUE(err.IsNone()) << aos::tests::utils::ErrorToStr(err);

    ASSERT_EQ(itemData->Size(), 1);
    EXPECT_EQ((*itemData)[0], item1v2);

    // Get item2

    itemData->Clear();

    err = mDB.GetUpdateItem("item2", *itemData);
    ASSERT_TRUE(err.Is(aos::ErrorEnum::eNotFound)) << aos::tests::utils::ErrorToStr(err);

    ASSERT_EQ(itemData->Size(), 0);
}

TEST_F(DatabaseTest, GetAllUpdateItems)
{
    ASSERT_TRUE(mDB.Init(mWorkingDir.string(), mMigrationConfig).IsNone());

    auto item1v1 = CreateUpdateItemData("item1", aos::UpdateItemTypeEnum::eService, "1.0.0", "sha256:digest1",
        aos::ItemStateEnum::eInstalled, aos::Time::Now());
    auto item1v2 = CreateUpdateItemData("item1", aos::UpdateItemTypeEnum::eService, "2.0.0", "sha256:digest2",
        aos::ItemStateEnum::eInstalled, aos::Time::Now());
    auto item2v1 = CreateUpdateItemData("item2", aos::UpdateItemTypeEnum::eService, "1.0.0", "sha256:digest3",
        aos::ItemStateEnum::eInstalled, aos::Time::Now());

    // Add item1 v1

    auto err = mDB.AddUpdateItem(item1v1);
    ASSERT_TRUE(err.IsNone()) << aos::tests::utils::ErrorToStr(err);

    // Add item1 v2

    err = mDB.AddUpdateItem(item1v2);
    ASSERT_TRUE(err.IsNone()) << aos::tests::utils::ErrorToStr(err);

    // Add item2 v1

    err = mDB.AddUpdateItem(item2v1);
    ASSERT_TRUE(err.IsNone()) << aos::tests::utils::ErrorToStr(err);

    // Get all update items

    auto itemsData = std::make_unique<aos::StaticArray<aos::sm::imagemanager::UpdateItemData, 3>>();

    err = mDB.GetAllUpdateItems(*itemsData);
    ASSERT_TRUE(err.IsNone()) << aos::tests::utils::ErrorToStr(err);

    ASSERT_EQ(itemsData->Size(), 3);
    EXPECT_EQ((*itemsData)[0], item1v1);
    EXPECT_EQ((*itemsData)[1], item1v2);
    EXPECT_EQ((*itemsData)[2], item2v1);

    size_t count = 0;

    Tie(count, err) = mDB.GetUpdateItemsCount();
    ASSERT_TRUE(err.IsNone()) << aos::tests::utils::ErrorToStr(err);

    EXPECT_EQ(count, itemsData->Size());
}

/***********************************************************************************************************************
 * Tests - launcher::StorageItf
 **********************************************************************************************************************/

TEST_F(DatabaseTest, UpdateInstanceInfo)
{
    ASSERT_TRUE(mDB.Init(mWorkingDir.string(), mMigrationConfig).IsNone());

    auto instanceInfo = CreateInstanceInfo("service-1", "subject-1", 1);

    auto err = mDB.UpdateInstanceInfo(instanceInfo);
    ASSERT_TRUE(err.IsNone()) << aos::tests::utils::ErrorToStr(err);

    err = mDB.UpdateInstanceInfo(instanceInfo);
    ASSERT_TRUE(err.IsNone()) << aos::tests::utils::ErrorToStr(err);
}

TEST_F(DatabaseTest, RemoveInstanceInfo)
{
    ASSERT_TRUE(mDB.Init(mWorkingDir.string(), mMigrationConfig).IsNone());

    auto ident = CreateInstanceIdent("unknown", "unknown", 0);

    ASSERT_TRUE(mDB.RemoveInstanceInfo(ident).Is(aos::ErrorEnum::eNotFound));

    auto instanceInfo = CreateInstanceInfo("service-1", "subject-1", 1);

    ASSERT_TRUE(mDB.UpdateInstanceInfo(instanceInfo).IsNone());

    ident = CreateInstanceIdent("service-1", "subject-1", 1);

    ASSERT_TRUE(mDB.RemoveInstanceInfo(ident).IsNone());
}

TEST_F(DatabaseTest, GetAllInstancesInfos)
{
    auto err = mDB.Init(mWorkingDir.string(), mMigrationConfig);
    ASSERT_TRUE(err.IsNone()) << aos::tests::utils::ErrorToStr(err);

    auto instanceInfo          = CreateInstanceInfo("service-1", "subject-1", 1);
    instanceInfo.mPreinstalled = true;

    err = mDB.UpdateInstanceInfo(instanceInfo);
    ASSERT_TRUE(err.IsNone()) << aos::tests::utils::ErrorToStr(err);

    // aos::InstanceInfoArray result;
    auto result = std::make_unique<aos::InstanceInfoArray>();

    err = mDB.GetAllInstancesInfos(*result);
    ASSERT_TRUE(err.IsNone()) << aos::tests::utils::ErrorToStr(err);

    ASSERT_EQ(result->Size(), 1);

    auto& resultRef = result->Back();

    EXPECT_EQ(resultRef.mItemID, instanceInfo.mItemID);
    EXPECT_EQ(resultRef.mSubjectID, instanceInfo.mSubjectID);
    EXPECT_EQ(resultRef.mInstance, instanceInfo.mInstance);
    EXPECT_EQ(resultRef.mType, instanceInfo.mType);
    EXPECT_TRUE(resultRef.mPreinstalled);
    EXPECT_EQ(resultRef.mManifestDigest, instanceInfo.mManifestDigest);
    EXPECT_EQ(resultRef.mRuntimeID, instanceInfo.mRuntimeID);
    EXPECT_EQ(resultRef.mSubjectType, instanceInfo.mSubjectType);
    EXPECT_EQ(resultRef.mUID, instanceInfo.mUID);
    EXPECT_EQ(resultRef.mGID, instanceInfo.mGID);
    EXPECT_EQ(resultRef.mPriority, instanceInfo.mPriority);
    EXPECT_EQ(resultRef.mStoragePath, instanceInfo.mStoragePath);
    EXPECT_EQ(resultRef.mStatePath, instanceInfo.mStatePath);
}

TEST_F(DatabaseTest, GetAllInstancesInfosWithComplexFields)
{
    ASSERT_TRUE(mDB.Init(mWorkingDir.string(), mMigrationConfig).IsNone());

    aos::InstanceInfo instanceInfo;

    instanceInfo.mItemID         = "service-1";
    instanceInfo.mSubjectID      = "subject-1";
    instanceInfo.mInstance       = 1;
    instanceInfo.mType           = aos::UpdateItemTypeEnum::eService;
    instanceInfo.mManifestDigest = "sha256:digest123";
    instanceInfo.mRuntimeID      = "runtime-1";
    instanceInfo.mSubjectType    = aos::SubjectTypeEnum::eUser;
    instanceInfo.mUID            = 1000;
    instanceInfo.mGID            = 1001;
    instanceInfo.mPriority       = 10;
    instanceInfo.mStoragePath    = "/storage";
    instanceInfo.mStatePath      = "/state";

    // Add env vars
    aos::EnvVar envVar1;
    envVar1.mName  = "VAR1";
    envVar1.mValue = "value1";
    instanceInfo.mEnvVars.PushBack(envVar1);

    aos::EnvVar envVar2;
    envVar2.mName  = "VAR2";
    envVar2.mValue = "value2";
    instanceInfo.mEnvVars.PushBack(envVar2);

    // Add network parameters
    instanceInfo.mNetworkParameters.EmplaceValue();
    instanceInfo.mNetworkParameters.GetValue().mNetworkID = "network-1";
    instanceInfo.mNetworkParameters.GetValue().mSubnet    = "192.168.1.0/24";
    instanceInfo.mNetworkParameters.GetValue().mIP        = "192.168.1.10";
    instanceInfo.mNetworkParameters.GetValue().mDNSServers.EmplaceBack("8.8.8.8");

    // Add monitoring params
    instanceInfo.mMonitoringParams.EmplaceValue();
    instanceInfo.mMonitoringParams.GetValue().mAlertRules.EmplaceValue();
    instanceInfo.mMonitoringParams.GetValue().mAlertRules.GetValue().mRAM.EmplaceValue();
    instanceInfo.mMonitoringParams.GetValue().mAlertRules.GetValue().mRAM.GetValue().mMinThreshold = 50;
    instanceInfo.mMonitoringParams.GetValue().mAlertRules.GetValue().mRAM.GetValue().mMaxThreshold = 90;
    instanceInfo.mMonitoringParams.GetValue().mAlertRules.GetValue().mRAM.GetValue().mMinTimeout
        = aos::Duration(1000000000);

    ASSERT_TRUE(mDB.UpdateInstanceInfo(instanceInfo).IsNone());

    auto result = std::make_unique<aos::InstanceInfoArray>();

    ASSERT_TRUE(mDB.GetAllInstancesInfos(*result).IsNone());

    ASSERT_EQ(result->Size(), 1);
    EXPECT_EQ((*result)[0], instanceInfo);
}

TEST_F(DatabaseTest, GetAllInstancesInfosExceedsLimit)
{
    ASSERT_TRUE(mDB.Init(mWorkingDir.string(), mMigrationConfig).IsNone());

    for (size_t i = 0; i < aos::cMaxNumInstances + 1; ++i) {
        auto instanceInfo = CreateInstanceInfo("service-1", "subject-1", i);

        ASSERT_TRUE(mDB.UpdateInstanceInfo(instanceInfo).IsNone());
    }

    // aos::InstanceInfoArray result;
    auto result = std::make_unique<aos::InstanceInfoArray>();

    EXPECT_TRUE(mDB.GetAllInstancesInfos(*result).Is(aos::ErrorEnum::eNoMemory));
}

/***********************************************************************************************************************
 * Tests - networkmanager::StorageItf
 **********************************************************************************************************************/

TEST_F(DatabaseTest, AddNetworkInfoSucceeds)
{
    ASSERT_TRUE(mDB.Init(mWorkingDir.string(), mMigrationConfig).IsNone());

    aos::sm::networkmanager::NetworkInfo networkParams {"networkID", "subnet", "ip", 1, "vlanIfName", "bridgeIfName"};

    ASSERT_TRUE(mDB.AddNetworkInfo(networkParams).IsNone());
    ASSERT_TRUE(mDB.AddNetworkInfo(networkParams).Is(aos::ErrorEnum::eFailed));

    ASSERT_TRUE(mDB.RemoveNetworkInfo(networkParams.mNetworkID).IsNone());
}

TEST_F(DatabaseTest, RemoveNetworkInfoReturnsNotFound)
{
    ASSERT_TRUE(mDB.Init(mWorkingDir.string(), mMigrationConfig).IsNone());

    ASSERT_TRUE(mDB.RemoveNetworkInfo("unknown").Is(aos::ErrorEnum::eNotFound));
}

TEST_F(DatabaseTest, GetNetworksInfoSucceeds)
{
    ASSERT_TRUE(mDB.Init(mWorkingDir.string(), mMigrationConfig).IsNone());

    aos::StaticArray<aos::sm::networkmanager::NetworkInfo, 2> networks;
    aos::StaticArray<aos::sm::networkmanager::NetworkInfo, 2> expectedNetworks;

    networks.PushBack({"networkID-1", "subnet", "ip", 1, "vlanIfName", "bridgeIfName"});
    networks.PushBack({"networkID-2", "subnet", "ip", 1, "vlanIfName", "bridgeIfName"});

    for (const auto& network : networks) {
        ASSERT_TRUE(mDB.AddNetworkInfo(network).IsNone());
    }

    ASSERT_TRUE(mDB.GetNetworksInfo(expectedNetworks).IsNone());

    EXPECT_EQ(networks, expectedNetworks) << "expected networks are not equal to the result";
}

TEST_F(DatabaseTest, AddInstanceNetworkInfo)
{
    ASSERT_TRUE(mDB.Init(mWorkingDir.string(), mMigrationConfig).IsNone());

    aos::sm::networkmanager::InstanceNetworkInfo info;
    info.mInstanceID = "instance-1";
    info.mNetworkID  = "network-1";

    ASSERT_TRUE(mDB.AddInstanceNetworkInfo(info).IsNone());
    EXPECT_TRUE(mDB.AddInstanceNetworkInfo(info).Is(aos::ErrorEnum::eFailed));
}

TEST_F(DatabaseTest, RemoveInstanceNetworkInfo)
{
    ASSERT_TRUE(mDB.Init(mWorkingDir.string(), mMigrationConfig).IsNone());

    aos::sm::networkmanager::InstanceNetworkInfo info;
    info.mInstanceID = "instance-1";
    info.mNetworkID  = "network-1";

    ASSERT_TRUE(mDB.AddInstanceNetworkInfo(info).IsNone());
    ASSERT_TRUE(mDB.RemoveInstanceNetworkInfo(info.mInstanceID).IsNone());
    EXPECT_TRUE(mDB.RemoveInstanceNetworkInfo(info.mInstanceID).Is(aos::ErrorEnum::eNotFound));
}

TEST_F(DatabaseTest, GetInstanceNetworksInfo)
{
    ASSERT_TRUE(mDB.Init(mWorkingDir.string(), mMigrationConfig).IsNone());

    aos::sm::networkmanager::InstanceNetworkInfo info1;
    info1.mInstanceID = "instance-1";
    info1.mNetworkID  = "network-1";

    aos::sm::networkmanager::InstanceNetworkInfo info2;
    info2.mInstanceID = "instance-2";
    info2.mNetworkID  = "network-2";

    ASSERT_TRUE(mDB.AddInstanceNetworkInfo(info1).IsNone());
    ASSERT_TRUE(mDB.AddInstanceNetworkInfo(info2).IsNone());

    aos::StaticArray<aos::sm::networkmanager::InstanceNetworkInfo, 2> result;

    ASSERT_TRUE(mDB.GetInstanceNetworksInfo(result).IsNone());

    ASSERT_EQ(result.Size(), 2);

    EXPECT_EQ(result[0].mInstanceID, info1.mInstanceID);
    EXPECT_EQ(result[0].mNetworkID, info1.mNetworkID);
    EXPECT_EQ(result[1].mInstanceID, info2.mInstanceID);
    EXPECT_EQ(result[1].mNetworkID, info2.mNetworkID);
}

TEST_F(DatabaseTest, GetInstanceNetworksInfoEmpty)
{
    ASSERT_TRUE(mDB.Init(mWorkingDir.string(), mMigrationConfig).IsNone());

    aos::StaticArray<aos::sm::networkmanager::InstanceNetworkInfo, 2> result;

    ASSERT_TRUE(mDB.GetInstanceNetworksInfo(result).IsNone());
    EXPECT_TRUE(result.IsEmpty());
}

TEST_F(DatabaseTest, SetUpdateAndRemoveTrafficMonitorDataSucceeds)
{
    ASSERT_TRUE(mDB.Init(mWorkingDir.string(), mMigrationConfig).IsNone());

    const aos::String chain = "chain";
    aos::Time         time  = aos::Time::Now();
    uint64_t          value = 100;

    ASSERT_TRUE(mDB.SetTrafficMonitorData(chain, time, value).IsNone());

    time  = aos::Time::Now();
    value = 200;

    ASSERT_TRUE(mDB.SetTrafficMonitorData(chain, time, value).IsNone());

    aos::Time resTime;
    uint64_t  resValue;

    ASSERT_TRUE(mDB.GetTrafficMonitorData(chain, resTime, resValue).IsNone());

    EXPECT_EQ(resValue, value) << "expected value is not equal to the result";
    EXPECT_EQ(resTime, time) << "expected time is not equal to the result";

    ASSERT_TRUE(mDB.RemoveTrafficMonitorData(chain).IsNone());
    ASSERT_TRUE(mDB.GetTrafficMonitorData(chain, resTime, resValue).Is(aos::ErrorEnum::eNotFound));
}

/***********************************************************************************************************************
 * Tests - alerts::StorageItf
 **********************************************************************************************************************/

TEST_F(DatabaseTest, JournalCursor)
{
    ASSERT_TRUE(mDB.Init(mWorkingDir.string(), mMigrationConfig).IsNone());

    aos::StaticString<32> journalCursor;

    ASSERT_TRUE(mDB.GetJournalCursor(journalCursor).IsNone());
    EXPECT_TRUE(journalCursor.IsEmpty());

    ASSERT_TRUE(mDB.SetJournalCursor("cursor").IsNone());

    ASSERT_TRUE(mDB.GetJournalCursor(journalCursor).IsNone());
    EXPECT_EQ(journalCursor, aos::String("cursor"));
}
