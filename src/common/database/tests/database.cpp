/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>

#include <gmock/gmock.h>

#include <Poco/Data/SQLite/Connector.h>

#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>

#include <common/database/database.hpp>
#include <common/utils/exception.hpp>

using namespace testing;

namespace aos::common::database {

namespace {

/***********************************************************************************************************************
 * Utils
 **********************************************************************************************************************/

template <typename T>
std::vector<T> ToVector(const Array<T>& src)
{
    return std::vector<T>(src.begin(), src.end());
}

common::imagemanager::DeployableItemInfo CreateDeployableItem(const char* itemID, const char* version,
    ItemState state = ItemStateEnum::eInstalled, UpdateItemType type = UpdateItemTypeEnum::eService,
    const char* rootDigest = "sha256:root", bool preinstalled = false, const Error& error = ErrorEnum::eNone)
{
    common::imagemanager::DeployableItemInfo item;

    item.mItemID       = itemID;
    item.mType         = type;
    item.mVersion      = version;
    item.mPreinstalled = preinstalled;
    item.mState        = state;
    item.mError        = error;
    item.mRootDigest   = rootDigest;
    item.mTimestamp    = Time::Now();

    return item;
}

common::imagemanager::ItemBlobInfo CreateItemBlob(
    const char* digest, bool isLayer = false, const char* diffID = "", uint64_t size = 1024)
{
    common::imagemanager::ItemBlobInfo blob;

    blob.mMediaType
        = isLayer ? "application/vnd.oci.image.layer.v1.tar+gzip" : "application/vnd.oci.image.manifest.v1+json";
    blob.mDigest  = digest;
    blob.mSize    = size;
    blob.mIsLayer = isLayer;
    blob.mDiffID  = diffID;

    return blob;
}

class TestDatabase : public Database {
public:
    TestDatabase() { Poco::Data::SQLite::Connector::registerConnector(); }

    ~TestDatabase()
    {
        if (mSession && mSession->isConnected()) {
            mSession->close();
        }

        Poco::Data::SQLite::Connector::unregisterConnector();
    }

    Error Init(const std::string& dbPath)
    {
        std::lock_guard lock {mMutex};

        try {
            mSession = std::make_unique<Poco::Data::Session>("SQLite", dbPath);

            *mSession << "CREATE TABLE IF NOT EXISTS imagemanager_items ("
                         "itemID TEXT,"
                         "type TEXT,"
                         "version TEXT,"
                         "preinstalled INTEGER,"
                         "state TEXT,"
                         "errorAosCode INTEGER,"
                         "errorSysCode INTEGER,"
                         "errorMessage TEXT,"
                         "rootDigest TEXT,"
                         "timestamp INTEGER,"
                         "PRIMARY KEY (itemID, version)"
                         ");",
                Poco::Data::Keywords::now;

            *mSession << "CREATE TABLE IF NOT EXISTS imagemanager_blobs ("
                         "itemID TEXT,"
                         "version TEXT,"
                         "digest TEXT,"
                         "mediaType TEXT,"
                         "size INTEGER,"
                         "isLayer INTEGER,"
                         "diffID TEXT,"
                         "PRIMARY KEY (itemID, version, digest)"
                         ");",
                Poco::Data::Keywords::now;

            *mSession << "CREATE INDEX IF NOT EXISTS imagemanager_blobs_digest ON imagemanager_blobs(digest);",
                Poco::Data::Keywords::now;
        } catch (const std::exception& e) {
            return AOS_ERROR_WRAP(common::utils::ToAosError(e));
        }

        return ErrorEnum::eNone;
    }
};

} // namespace

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class CommonDatabaseTest : public Test {
protected:
    static void SetUpTestSuite() { aos::tests::utils::InitLog(); }

    void SetUp() override
    {
        TearDown();

        std::filesystem::create_directories(cWorkingDir);
    }

    void TearDown() override { std::filesystem::remove_all(cWorkingDir); }

    static constexpr auto cWorkingDir = "common_database_test";
    static constexpr auto cDBPath     = "common_database_test/test.db";

    TestDatabase mDB;
};

/***********************************************************************************************************************
 * common::imagemanager::StorageItf tests
 **********************************************************************************************************************/

TEST_F(CommonDatabaseTest, AddDeployableItem)
{
    ASSERT_TRUE(mDB.Init(cDBPath).IsNone());

    auto item = CreateDeployableItem("service1", "1.0.0");

    ASSERT_TRUE(mDB.AddDeployableItem(item).IsNone());

    auto duplicate = CreateDeployableItem("service1", "1.0.0", ItemStateEnum::ePending);

    EXPECT_TRUE(mDB.AddDeployableItem(duplicate).Is(ErrorEnum::eAlreadyExist));

    common::imagemanager::DeployableItemInfo result;

    ASSERT_TRUE(mDB.GetDeployableItem("service1", "1.0.0", result).IsNone());
    EXPECT_EQ(result, item);
}

TEST_F(CommonDatabaseTest, DeployableItemRoundTrip)
{
    ASSERT_TRUE(mDB.Init(cDBPath).IsNone());

    auto item = CreateDeployableItem("component1", "2.1.0", ItemStateEnum::eFailed, UpdateItemTypeEnum::eComponent,
        "sha256:abcdef", true, Error(ErrorEnum::eRuntime, 42, "install failed"));

    ASSERT_TRUE(mDB.AddDeployableItem(item).IsNone());

    common::imagemanager::DeployableItemInfo result;

    ASSERT_TRUE(mDB.GetDeployableItem("component1", "2.1.0", result).IsNone());

    EXPECT_EQ(result, item);
    EXPECT_EQ(result.mItemID, "component1");
    EXPECT_EQ(result.mType, UpdateItemTypeEnum::eComponent);
    EXPECT_EQ(result.mVersion, "2.1.0");
    EXPECT_TRUE(result.mPreinstalled);
    EXPECT_EQ(result.mState, ItemStateEnum::eFailed);
    EXPECT_EQ(result.mError.Value(), ErrorEnum::eRuntime);
    EXPECT_EQ(result.mError.Code(), 42);
    EXPECT_STREQ(result.mError.Message(), "install failed");
    EXPECT_EQ(result.mRootDigest, "sha256:abcdef");
    EXPECT_EQ(result.mTimestamp, item.mTimestamp);

    auto noError = CreateDeployableItem("service1", "1.0.0");

    ASSERT_TRUE(mDB.AddDeployableItem(noError).IsNone());
    ASSERT_TRUE(mDB.GetDeployableItem("service1", "1.0.0", result).IsNone());

    EXPECT_TRUE(result.mError.IsNone());
    EXPECT_EQ(result.mError.Code(), 0);
    EXPECT_FALSE(result.mPreinstalled);
}

TEST_F(CommonDatabaseTest, UpdateDeployableItem)
{
    ASSERT_TRUE(mDB.Init(cDBPath).IsNone());

    auto item = CreateDeployableItem("service1", "1.0.0", ItemStateEnum::ePending);

    ASSERT_TRUE(mDB.AddDeployableItem(item).IsNone());
    ASSERT_TRUE(mDB.AddItemBlob("service1", "1.0.0", CreateItemBlob("sha256:manifest")).IsNone());

    item.mState      = ItemStateEnum::eFailed;
    item.mError      = Error(ErrorEnum::eFailed, 5, "update failed");
    item.mRootDigest = "sha256:newroot";
    item.mTimestamp  = Time::Now();

    ASSERT_TRUE(mDB.UpdateDeployableItem(item).IsNone());

    common::imagemanager::DeployableItemInfo result;

    ASSERT_TRUE(mDB.GetDeployableItem("service1", "1.0.0", result).IsNone());
    EXPECT_EQ(result, item);
    EXPECT_EQ(result.mError.Code(), 5);
    EXPECT_STREQ(result.mError.Message(), "update failed");

    StaticArray<common::imagemanager::ItemBlobInfo, 1> blobs;

    ASSERT_TRUE(mDB.GetItemBlobs("service1", "1.0.0", blobs).IsNone());
    EXPECT_THAT(ToVector(blobs), ElementsAre(CreateItemBlob("sha256:manifest")));

    EXPECT_TRUE(mDB.UpdateDeployableItem(CreateDeployableItem("missing", "1.0.0")).Is(ErrorEnum::eNotFound));
}

TEST_F(CommonDatabaseTest, GetDeployableItemNotFound)
{
    ASSERT_TRUE(mDB.Init(cDBPath).IsNone());

    ASSERT_TRUE(mDB.AddDeployableItem(CreateDeployableItem("service1", "1.0.0")).IsNone());

    common::imagemanager::DeployableItemInfo result;

    EXPECT_TRUE(mDB.GetDeployableItem("service1", "2.0.0", result).Is(ErrorEnum::eNotFound));
    EXPECT_TRUE(mDB.GetDeployableItem("missing", "1.0.0", result).Is(ErrorEnum::eNotFound));
}

TEST_F(CommonDatabaseTest, GetAllDeployableItemsAndCount)
{
    ASSERT_TRUE(mDB.Init(cDBPath).IsNone());

    StaticArray<common::imagemanager::DeployableItemInfo, 3> items;

    ASSERT_TRUE(mDB.GetAllDeployableItems(items).IsNone());
    EXPECT_TRUE(items.IsEmpty());

    auto count = mDB.GetDeployableItemsCount();

    ASSERT_TRUE(count.mError.IsNone());
    EXPECT_EQ(count.mValue, 0U);

    auto item1 = CreateDeployableItem("service1", "1.0.0", ItemStateEnum::eInstalled);
    auto item2 = CreateDeployableItem("service1", "2.0.0", ItemStateEnum::ePending);
    auto item3 = CreateDeployableItem("service2", "1.0.0", ItemStateEnum::eRemoved);

    ASSERT_TRUE(mDB.AddDeployableItem(item1).IsNone());
    ASSERT_TRUE(mDB.AddDeployableItem(item2).IsNone());
    ASSERT_TRUE(mDB.AddDeployableItem(item3).IsNone());

    ASSERT_TRUE(mDB.GetAllDeployableItems(items).IsNone());
    EXPECT_THAT(ToVector(items), UnorderedElementsAre(item1, item2, item3));

    count = mDB.GetDeployableItemsCount();

    ASSERT_TRUE(count.mError.IsNone());
    EXPECT_EQ(count.mValue, 3U);
}

TEST_F(CommonDatabaseTest, RemoveDeployableItem)
{
    ASSERT_TRUE(mDB.Init(cDBPath).IsNone());

    auto item1 = CreateDeployableItem("service1", "1.0.0");
    auto item2 = CreateDeployableItem("service1", "2.0.0");

    ASSERT_TRUE(mDB.AddDeployableItem(item1).IsNone());
    ASSERT_TRUE(mDB.AddDeployableItem(item2).IsNone());
    ASSERT_TRUE(mDB.AddItemBlob("service1", "1.0.0", CreateItemBlob("sha256:manifest1")).IsNone());
    ASSERT_TRUE(mDB.AddItemBlob("service1", "1.0.0", CreateItemBlob("sha256:layer", true, "sha256:diff")).IsNone());
    ASSERT_TRUE(mDB.AddItemBlob("service1", "2.0.0", CreateItemBlob("sha256:layer", true, "sha256:diff")).IsNone());

    ASSERT_TRUE(mDB.RemoveDeployableItem("service1", "1.0.0").IsNone());

    common::imagemanager::DeployableItemInfo result;

    EXPECT_TRUE(mDB.GetDeployableItem("service1", "1.0.0", result).Is(ErrorEnum::eNotFound));

    StaticArray<common::imagemanager::ItemBlobInfo, 2> blobs;

    EXPECT_TRUE(mDB.GetItemBlobs("service1", "1.0.0", blobs).Is(ErrorEnum::eNotFound));

    auto used = mDB.IsBlobUsed("sha256:manifest1");

    ASSERT_TRUE(used.mError.IsNone());
    EXPECT_FALSE(used.mValue);

    used = mDB.IsBlobUsed("sha256:layer");

    ASSERT_TRUE(used.mError.IsNone());
    EXPECT_TRUE(used.mValue);

    ASSERT_TRUE(mDB.AddDeployableItem(item1).IsNone());
    ASSERT_TRUE(mDB.GetItemBlobs("service1", "1.0.0", blobs).IsNone());
    EXPECT_TRUE(blobs.IsEmpty());

    EXPECT_TRUE(mDB.RemoveDeployableItem("missing", "1.0.0").Is(ErrorEnum::eNotFound));

    auto count = mDB.GetDeployableItemsCount();

    ASSERT_TRUE(count.mError.IsNone());
    EXPECT_EQ(count.mValue, 2U);
}

TEST_F(CommonDatabaseTest, RemoveDeployableItemRollback)
{
    ASSERT_TRUE(mDB.Init(cDBPath).IsNone());

    auto item = CreateDeployableItem("service1", "1.0.0");
    auto blob = CreateItemBlob("sha256:manifest");

    ASSERT_TRUE(mDB.AddDeployableItem(item).IsNone());
    ASSERT_TRUE(mDB.AddItemBlob("service1", "1.0.0", blob).IsNone());

    Poco::Data::Session session("SQLite", cDBPath);

    session << "CREATE TRIGGER fail_blob_delete BEFORE DELETE ON imagemanager_blobs "
               "BEGIN SELECT RAISE(ABORT, 'fail'); END;",
        Poco::Data::Keywords::now;

    EXPECT_FALSE(mDB.RemoveDeployableItem("service1", "1.0.0").IsNone());

    common::imagemanager::DeployableItemInfo result;

    ASSERT_TRUE(mDB.GetDeployableItem("service1", "1.0.0", result).IsNone());
    EXPECT_EQ(result, item);

    StaticArray<common::imagemanager::ItemBlobInfo, 1> blobs;

    ASSERT_TRUE(mDB.GetItemBlobs("service1", "1.0.0", blobs).IsNone());
    EXPECT_THAT(ToVector(blobs), ElementsAre(blob));

    session << "DROP TRIGGER fail_blob_delete;", Poco::Data::Keywords::now;
}

TEST_F(CommonDatabaseTest, AddItemBlob)
{
    ASSERT_TRUE(mDB.Init(cDBPath).IsNone());

    auto manifest = CreateItemBlob("sha256:manifest");
    auto layer    = CreateItemBlob("sha256:layer", true, "sha256:diff", 4096);

    EXPECT_TRUE(mDB.AddItemBlob("service1", "1.0.0", manifest).Is(ErrorEnum::eNotFound));

    ASSERT_TRUE(mDB.AddDeployableItem(CreateDeployableItem("service1", "1.0.0")).IsNone());

    ASSERT_TRUE(mDB.AddItemBlob("service1", "1.0.0", manifest).IsNone());
    ASSERT_TRUE(mDB.AddItemBlob("service1", "1.0.0", layer).IsNone());
    ASSERT_TRUE(mDB.AddItemBlob("service1", "1.0.0", manifest).IsNone());

    StaticArray<common::imagemanager::ItemBlobInfo, 3> blobs;

    ASSERT_TRUE(mDB.GetItemBlobs("service1", "1.0.0", blobs).IsNone());
    EXPECT_THAT(ToVector(blobs), UnorderedElementsAre(manifest, layer));
}

TEST_F(CommonDatabaseTest, GetItemBlobsNotFound)
{
    ASSERT_TRUE(mDB.Init(cDBPath).IsNone());

    StaticArray<common::imagemanager::ItemBlobInfo, 1> blobs;

    EXPECT_TRUE(mDB.GetItemBlobs("missing", "1.0.0", blobs).Is(ErrorEnum::eNotFound));

    ASSERT_TRUE(mDB.AddDeployableItem(CreateDeployableItem("service1", "1.0.0")).IsNone());

    ASSERT_TRUE(mDB.GetItemBlobs("service1", "1.0.0", blobs).IsNone());
    EXPECT_TRUE(blobs.IsEmpty());
}

TEST_F(CommonDatabaseTest, IsBlobUsed)
{
    ASSERT_TRUE(mDB.Init(cDBPath).IsNone());

    ASSERT_TRUE(mDB.AddDeployableItem(CreateDeployableItem("service1", "1.0.0", ItemStateEnum::eRemoved)).IsNone());
    ASSERT_TRUE(mDB.AddItemBlob("service1", "1.0.0", CreateItemBlob("sha256:used")).IsNone());

    {
        Poco::Data::Session session("SQLite", cDBPath);

        session << "INSERT INTO imagemanager_blobs (itemID, version, digest, mediaType, size, isLayer, diffID) "
                   "VALUES ('orphan', '1.0.0', 'sha256:orphan', 'application/octet-stream', 1, 0, '');",
            Poco::Data::Keywords::now;
    }

    auto used = mDB.IsBlobUsed("sha256:used");

    ASSERT_TRUE(used.mError.IsNone());
    EXPECT_TRUE(used.mValue);

    used = mDB.IsBlobUsed("sha256:unknown");

    ASSERT_TRUE(used.mError.IsNone());
    EXPECT_FALSE(used.mValue);

    used = mDB.IsBlobUsed("sha256:orphan");

    ASSERT_TRUE(used.mError.IsNone());
    EXPECT_FALSE(used.mValue);
}

} // namespace aos::common::database
