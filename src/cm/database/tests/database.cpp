/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gmock/gmock.h>

#include <cm/database/database.hpp>
#include <common/utils/exception.hpp>

using namespace testing;

namespace aos::cm::database {

namespace {

/***********************************************************************************************************************
 * Utils
 **********************************************************************************************************************/

template <typename T>
std::vector<T> ToVector(const Array<T>& src)
{
    return std::vector<T>(src.begin(), src.end());
}

InstanceIdent CreateInstanceIdent(const char* itemID, const char* subjectID, uint64_t instance)
{
    InstanceIdent ident;

    ident.mItemID    = itemID;
    ident.mSubjectID = subjectID;
    ident.mInstance  = instance;

    return ident;
}

storagestate::InstanceInfo CreateStorageStateInstanceInfo(
    const char* itemID, const char* subjectID, uint64_t instance, size_t storageQuota, size_t stateQuota)
{
    storagestate::InstanceInfo info;
    std::vector<uint8_t>       checksum = {0xde, 0xad, 0xbe, 0xef};

    info.mInstanceIdent = CreateInstanceIdent(itemID, subjectID, instance);
    info.mStorageQuota  = storageQuota;
    info.mStateQuota    = stateQuota;
    info.mStateChecksum = Array<uint8_t>(checksum.data(), checksum.size());

    return info;
}

imagemanager::storage::ItemInfo CreateImageManagerItemInfo(
    const char* id, const char* version, imagemanager::storage::ItemStateEnum state, size_t totalSize)
{
    imagemanager::storage::ItemInfo item;

    item.mID        = id;
    item.mVersion   = version;
    item.mTotalSize = totalSize;
    item.mGID       = 1000;
    item.mPath      = "/path/to/item";
    item.mTimestamp = Time::Now();

    item.mType  = UpdateItemTypeEnum::eComponent;
    item.mState = state;

    // Add a sample image
    imagemanager::storage::ImageInfo imageInfo;
    imageInfo.mURL  = "http://example.com/image.tar";
    imageInfo.mPath = "/path/to/image";
    imageInfo.mSize = 1024 * 1024;

    // SHA256 hash (magic number)
    static const std::vector<uint8_t> cSHA256MagicNumber = {0xde, 0xad, 0xbe, 0xef};

    imageInfo.mSHA256 = Array<uint8_t>(cSHA256MagicNumber.data(), cSHA256MagicNumber.size());

    // Platform info
    imageInfo.mImageID                = "image1";
    imageInfo.mArchInfo.mArchitecture = "amd64";
    imageInfo.mArchInfo.mVariant.SetValue("v8");
    imageInfo.mOSInfo.mOS = "linux";
    imageInfo.mOSInfo.mVersion.SetValue("5.10");

    AOS_ERROR_CHECK_AND_THROW(item.mImages.PushBack(imageInfo), "can't add image");

    return item;
}

networkmanager::Network CreateNetwork(const char* networkID, const char* subnet, uint64_t vlanID)
{
    networkmanager::Network network;

    network.mNetworkID = networkID;
    network.mSubnet    = subnet;
    network.mVlanID    = vlanID;

    return network;
}

networkmanager::Host CreateHost(const char* nodeID, const char* ip)
{
    networkmanager::Host host;

    host.mNodeID = nodeID;
    host.mIP     = ip;

    return host;
}

networkmanager::Instance CreateInstance(const char* itemID, const char* subjectID, uint64_t instance,
    const char* networkID, const char* nodeID, const char* ip)
{
    networkmanager::Instance inst;

    inst.mInstanceIdent = CreateInstanceIdent(itemID, subjectID, instance);
    inst.mNetworkID     = networkID;
    inst.mNodeID        = nodeID;
    inst.mIP            = ip;

    // Add sample exposed ports
    networkmanager::ExposedPort port1;
    port1.mProtocol = "tcp";
    port1.mPort     = "8080";
    AOS_ERROR_CHECK_AND_THROW(inst.mExposedPorts.PushBack(port1), "can't add exposed port");

    networkmanager::ExposedPort port2;
    port2.mProtocol = "udp";
    port2.mPort     = "9090";
    AOS_ERROR_CHECK_AND_THROW(inst.mExposedPorts.PushBack(port2), "can't add exposed port");

    // Add sample DNS servers
    AOS_ERROR_CHECK_AND_THROW(inst.mDNSServers.EmplaceBack("8.8.8.8"), "can't add DNS server");
    AOS_ERROR_CHECK_AND_THROW(inst.mDNSServers.EmplaceBack("1.1.1.1"), "can't add DNS server");

    return inst;
}

launcher::InstanceInfo CreateLauncherInstanceInfo(
    const char* itemID, const char* subjectID, uint64_t instance, const char* imageID, const char* nodeID)
{
    launcher::InstanceInfo info;

    info.mInstanceIdent  = CreateInstanceIdent(itemID, subjectID, instance);
    info.mImageID        = imageID;
    info.mUpdateItemType = UpdateItemTypeEnum::eService;
    info.mNodeID         = nodeID;
    info.mPrevNodeID     = "prevNode";
    info.mRuntimeID      = "runc";
    info.mUID            = 1000;
    info.mTimestamp      = Time::Now();
    info.mCached         = true;

    return info;
}

std::string GetMigrationSourceDir()
{
    std::filesystem::path curFilePath(__FILE__);
    std::filesystem::path migrationSourceDir = curFilePath.parent_path() / "../" / "migration/";

    return std::filesystem::canonical(migrationSourceDir).string();
}

class TestDatabase : public Database {
public:
    void SetVersion(int version) { mVersion = version; }

private:
    int GetVersion() const override { return mVersion; }

    int mVersion = 0;
};

} // namespace

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class CMDatabaseTest : public Test {
protected:
    void SetUp() override
    {
        // Clean up on start.
        TearDown();

        namespace fs = std::filesystem;

        auto migrationSrc = GetMigrationSourceDir();
        auto migrationDst = fs::current_path() / cMigrationPath;
        auto workingDir   = fs::current_path() / cWorkingDir;

        mDatabaseConfig.mWorkingDir          = workingDir;
        mDatabaseConfig.mMigrationPath       = cMigrationPath;
        mDatabaseConfig.mMergedMigrationPath = cMergedMigrationPath;

        fs::create_directories(cMigrationPath);

        fs::copy(migrationSrc, migrationDst, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
    }

    void TearDown() override { std::filesystem::remove_all(cWorkingDir); }

protected:
    static constexpr auto cWorkingDir          = "cm_database_test";
    static constexpr auto cMigrationPath       = "cm_database_test/migration";
    static constexpr auto cMergedMigrationPath = "cm_database_test/merged-migration";

    Config       mDatabaseConfig;
    TestDatabase mDB;
};

/***********************************************************************************************************************
 * storagestate::StorageItf tests
 **********************************************************************************************************************/

TEST_F(CMDatabaseTest, StateStorageAddStorageStateInfo)
{
    ASSERT_TRUE(mDB.Init(mDatabaseConfig).IsNone());

    auto info1 = CreateStorageStateInstanceInfo("service1", "subject1", 0, 1024 * 1024, 512 * 1024);
    auto info2 = CreateStorageStateInstanceInfo("service1", "subject2", 0, 2048 * 1024, 1024 * 1024);

    ASSERT_TRUE(mDB.AddStorageStateInfo(info1).IsNone());
    ASSERT_TRUE(mDB.AddStorageStateInfo(info2).IsNone());

    auto infoDuplicate = CreateStorageStateInstanceInfo("service1", "subject1", 0, 9999, 9999);
    ASSERT_FALSE(mDB.AddStorageStateInfo(infoDuplicate).IsNone());

    storagestate::InstanceInfo resultInfo;

    ASSERT_TRUE(mDB.GetStorageStateInfo(info1.mInstanceIdent, resultInfo).IsNone());
    EXPECT_EQ(resultInfo, info1);

    ASSERT_TRUE(mDB.GetStorageStateInfo(info2.mInstanceIdent, resultInfo).IsNone());
    EXPECT_EQ(resultInfo, info2);
}

TEST_F(CMDatabaseTest, StateStorageRemoveStorageStateInfo)
{
    ASSERT_TRUE(mDB.Init(mDatabaseConfig).IsNone());

    auto info1 = CreateStorageStateInstanceInfo("service1", "subject1", 0, 1024 * 1024, 512 * 1024);
    auto info2 = CreateStorageStateInstanceInfo("service1", "subject2", 0, 2048 * 1024, 1024 * 1024);
    auto info3 = CreateStorageStateInstanceInfo("service2", "subject1", 1, 512 * 1024, 256 * 1024);

    ASSERT_TRUE(mDB.AddStorageStateInfo(info1).IsNone());
    ASSERT_TRUE(mDB.AddStorageStateInfo(info2).IsNone());
    ASSERT_TRUE(mDB.AddStorageStateInfo(info3).IsNone());

    // Remove info2
    ASSERT_TRUE(mDB.RemoveStorageStateInfo(info2.mInstanceIdent).IsNone());

    storagestate::InstanceInfo resultInfo;
    ASSERT_FALSE(mDB.GetStorageStateInfo(info2.mInstanceIdent, resultInfo).IsNone());

    // Remove second time should fail
    ASSERT_FALSE(mDB.RemoveStorageStateInfo(info2.mInstanceIdent).IsNone());

    // Verify info1 and info3 still exist
    ASSERT_TRUE(mDB.GetStorageStateInfo(info1.mInstanceIdent, resultInfo).IsNone());
    EXPECT_EQ(resultInfo, info1);

    ASSERT_TRUE(mDB.GetStorageStateInfo(info3.mInstanceIdent, resultInfo).IsNone());
    EXPECT_EQ(resultInfo, info3);
}

TEST_F(CMDatabaseTest, StateStorageGetAllStorageStateInfo)
{
    ASSERT_TRUE(mDB.Init(mDatabaseConfig).IsNone());

    // Test with empty database
    StaticArray<storagestate::InstanceInfo, 10> allInfos;
    ASSERT_TRUE(mDB.GetAllStorageStateInfo(allInfos).IsNone());
    EXPECT_EQ(allInfos.Size(), 0);

    // Add multiple infos
    auto info1 = CreateStorageStateInstanceInfo("service1", "subject1", 0, 1024 * 1024, 512 * 1024);
    auto info2 = CreateStorageStateInstanceInfo("service1", "subject2", 0, 2048 * 1024, 1024 * 1024);
    auto info3 = CreateStorageStateInstanceInfo("service2", "subject1", 1, 512 * 1024, 256 * 1024);

    ASSERT_TRUE(mDB.AddStorageStateInfo(info1).IsNone());
    ASSERT_TRUE(mDB.AddStorageStateInfo(info2).IsNone());
    ASSERT_TRUE(mDB.AddStorageStateInfo(info3).IsNone());

    ASSERT_TRUE(mDB.GetAllStorageStateInfo(allInfos).IsNone());
    EXPECT_THAT(ToVector(allInfos), UnorderedElementsAre(info1, info2, info3));
}

TEST_F(CMDatabaseTest, StateStorageUpdateStorageStateInfo)
{
    ASSERT_TRUE(mDB.Init(mDatabaseConfig).IsNone());

    auto info = CreateStorageStateInstanceInfo("service1", "subject1", 0, 1024 * 1024, 512 * 1024);

    ASSERT_TRUE(mDB.AddStorageStateInfo(info).IsNone());

    // Update the info with new values
    std::vector<uint8_t> checksum = {0xde, 0xad, 0xbe, 0xef};

    info.mStorageQuota  = 2048 * 1024;
    info.mStateQuota    = 1024 * 1024;
    info.mStateChecksum = Array<uint8_t>(checksum.data(), checksum.size());

    ASSERT_TRUE(mDB.UpdateStorageStateInfo(info).IsNone());

    // Verify the info was updated
    storagestate::InstanceInfo resultInfo;
    ASSERT_TRUE(mDB.GetStorageStateInfo(info.mInstanceIdent, resultInfo).IsNone());
    EXPECT_EQ(resultInfo, info);

    auto nonExistentInfo = CreateStorageStateInstanceInfo("nonexistent", "subject", 99, 1024, 512);
    ASSERT_FALSE(mDB.UpdateStorageStateInfo(nonExistentInfo).IsNone());
}

/***********************************************************************************************************************
 * imagemanager::storage::StorageItf tests
 **********************************************************************************************************************/

TEST_F(CMDatabaseTest, ImageManagerAddItem)
{
    ASSERT_TRUE(mDB.Init(mDatabaseConfig).IsNone());

    auto item
        = CreateImageManagerItemInfo("item1", "1.0.0", imagemanager::storage::ItemStateEnum::eCached, 1024 * 1024);

    ASSERT_TRUE(mDB.AddItem(item).IsNone());

    // Verify the item was added by retrieving all items
    StaticArray<imagemanager::storage::ItemInfo, 10> allItems;
    ASSERT_TRUE(mDB.GetItemsInfo(allItems).IsNone());
    EXPECT_THAT(ToVector(allItems), UnorderedElementsAre(item));
}

TEST_F(CMDatabaseTest, ImageManagerSetItemState)
{
    ASSERT_TRUE(mDB.Init(mDatabaseConfig).IsNone());

    // Set state for non-existent item
    auto err = mDB.SetItemState(
        "nonexistent", "1.0.0", imagemanager::storage::ItemState(imagemanager::storage::ItemStateEnum::eActive));
    ASSERT_EQ(err, ErrorEnum::eNotFound);

    // Add an item with state "cached"
    auto item
        = CreateImageManagerItemInfo("item1", "1.0.0", imagemanager::storage::ItemStateEnum::eCached, 1024 * 1024);
    ASSERT_TRUE(mDB.AddItem(item).IsNone());

    StaticArray<imagemanager::storage::ItemInfo, 10> allItems;
    ASSERT_TRUE(mDB.GetItemsInfo(allItems).IsNone());
    ASSERT_EQ(allItems.Size(), 1);
    EXPECT_EQ(allItems[0].mState, imagemanager::storage::ItemStateEnum::eCached);

    // Set state to "active"
    ASSERT_TRUE(mDB.SetItemState(item.mID, item.mVersion, imagemanager::storage::ItemStateEnum::eActive).IsNone());

    ASSERT_TRUE(mDB.GetItemsInfo(allItems).IsNone());
    ASSERT_EQ(allItems.Size(), 1);
    EXPECT_EQ(allItems[0].mState, imagemanager::storage::ItemStateEnum::eActive);
}

TEST_F(CMDatabaseTest, ImageManagerRemoveItem)
{
    ASSERT_TRUE(mDB.Init(mDatabaseConfig).IsNone());

    // Remove non-existent item
    EXPECT_EQ(mDB.RemoveItem("nonexistent", "1.0.0"), ErrorEnum::eNotFound);

    // Add items
    auto item1
        = CreateImageManagerItemInfo("item1", "1.0.0", imagemanager::storage::ItemStateEnum::eCached, 1024 * 1024);
    auto item2
        = CreateImageManagerItemInfo("item2", "2.0.0", imagemanager::storage::ItemStateEnum::eActive, 2048 * 1024);

    ASSERT_TRUE(mDB.AddItem(item1).IsNone());
    ASSERT_TRUE(mDB.AddItem(item2).IsNone());

    // Remove item2
    ASSERT_TRUE(mDB.RemoveItem(item2.mID, item2.mVersion).IsNone());

    StaticArray<imagemanager::storage::ItemInfo, 10> allItems;
    ASSERT_TRUE(mDB.GetItemsInfo(allItems).IsNone());
    EXPECT_THAT(ToVector(allItems), UnorderedElementsAre(item1));

    // Remove item2 again
    EXPECT_EQ(mDB.RemoveItem(item2.mID, item2.mVersion), ErrorEnum::eNotFound);
}

TEST_F(CMDatabaseTest, ImageManagerGetItemVersionsByID)
{
    ASSERT_TRUE(mDB.Init(mDatabaseConfig).IsNone());

    // Get versions for non-existent item - should return empty
    StaticArray<imagemanager::storage::ItemInfo, 10> versions;
    ASSERT_TRUE(mDB.GetItemVersionsByID("nonexistent", versions).IsNone());
    EXPECT_EQ(versions.Size(), 0);

    // Add items
    auto item1v1
        = CreateImageManagerItemInfo("item1", "1.0.0", imagemanager::storage::ItemStateEnum::eCached, 1024 * 1024);
    auto item1v2
        = CreateImageManagerItemInfo("item1", "2.0.0", imagemanager::storage::ItemStateEnum::eActive, 2048 * 1024);
    auto item1v3
        = CreateImageManagerItemInfo("item1", "3.0.0", imagemanager::storage::ItemStateEnum::eCached, 3072 * 1024);
    auto item2v1
        = CreateImageManagerItemInfo("item2", "1.0.0", imagemanager::storage::ItemStateEnum::eActive, 512 * 1024);

    ASSERT_TRUE(mDB.AddItem(item1v1).IsNone());
    ASSERT_TRUE(mDB.AddItem(item1v2).IsNone());
    ASSERT_TRUE(mDB.AddItem(item1v3).IsNone());
    ASSERT_TRUE(mDB.AddItem(item2v1).IsNone());

    // Get all versions of item1
    ASSERT_TRUE(mDB.GetItemVersionsByID("item1", versions).IsNone());
    EXPECT_THAT(ToVector(versions), UnorderedElementsAre(item1v1, item1v2, item1v3));

    // Get all versions of item2
    ASSERT_TRUE(mDB.GetItemVersionsByID("item2", versions).IsNone());
    EXPECT_THAT(ToVector(versions), UnorderedElementsAre(item2v1));
}

/***********************************************************************************************************************
 * networkmanager::StorageItf tests
 **********************************************************************************************************************/

TEST_F(CMDatabaseTest, NetworkManagerAddNetwork)
{
    ASSERT_TRUE(mDB.Init(mDatabaseConfig).IsNone());

    auto network1 = CreateNetwork("network1", "172.17.0.0/16", 1000);
    auto network2 = CreateNetwork("network2", "172.18.0.0/16", 2000);
    auto network3 = CreateNetwork("network3", "10.0.0.0/8", 3000);

    // Add networks
    ASSERT_TRUE(mDB.AddNetwork(network1).IsNone());
    ASSERT_TRUE(mDB.AddNetwork(network2).IsNone());
    ASSERT_TRUE(mDB.AddNetwork(network3).IsNone());

    // Add duplicate network
    auto duplicateNetwork = CreateNetwork("network1", "192.168.0.0/16", 4000);
    ASSERT_FALSE(mDB.AddNetwork(duplicateNetwork).IsNone());

    // Verify networks
    StaticArray<networkmanager::Network, 3> networks;
    ASSERT_TRUE(mDB.GetNetworks(networks).IsNone());

    EXPECT_THAT(ToVector(networks), UnorderedElementsAre(network1, network2, network3));
}

TEST_F(CMDatabaseTest, NetworkManagerAddHost)
{
    ASSERT_TRUE(mDB.Init(mDatabaseConfig).IsNone());

    // Create a network
    auto network = CreateNetwork("network1", "172.17.0.0/16", 1000);
    ASSERT_TRUE(mDB.AddNetwork(network).IsNone());

    auto host1 = CreateHost("node1", "172.17.0.2");
    auto host2 = CreateHost("node2", "172.17.0.3");
    auto host3 = CreateHost("node3", "172.17.0.4");

    // Add hosts
    ASSERT_TRUE(mDB.AddHost("network1", host1).IsNone());
    ASSERT_TRUE(mDB.AddHost("network1", host2).IsNone());
    ASSERT_TRUE(mDB.AddHost("network1", host3).IsNone());

    // Add duplicate host
    auto duplicateHost = CreateHost("node1", "172.17.0.5");
    ASSERT_FALSE(mDB.AddHost("network1", duplicateHost).IsNone());

    // Add host to non-existent network
    ASSERT_FALSE(mDB.AddHost("nonexistent", host1).IsNone());

    // Verify hosts
    StaticArray<networkmanager::Host, 3> hosts;

    ASSERT_TRUE(mDB.GetHosts("network1", hosts).IsNone());
    EXPECT_THAT(ToVector(hosts), UnorderedElementsAre(host1, host2, host3));
}

TEST_F(CMDatabaseTest, NetworkManagerAddInstance)
{
    ASSERT_TRUE(mDB.Init(mDatabaseConfig).IsNone());

    // Create network and host
    auto network = CreateNetwork("network1", "172.17.0.0/16", 1000);
    ASSERT_TRUE(mDB.AddNetwork(network).IsNone());

    auto host = CreateHost("node1", "172.17.0.2");
    ASSERT_TRUE(mDB.AddHost("network1", host).IsNone());

    auto instance1 = CreateInstance("service1", "subject1", 0, "network1", "node1", "172.17.0.10");
    auto instance2 = CreateInstance("service1", "subject1", 1, "network1", "node1", "172.17.0.11");
    auto instance3 = CreateInstance("service2", "subject2", 0, "network1", "node1", "172.17.0.12");

    // Add instances
    ASSERT_TRUE(mDB.AddInstance(instance1).IsNone());
    ASSERT_TRUE(mDB.AddInstance(instance2).IsNone());
    ASSERT_TRUE(mDB.AddInstance(instance3).IsNone());

    // Add duplicate instance
    auto duplicateInstance = CreateInstance("service1", "subject1", 0, "network1", "node1", "172.17.0.99");
    ASSERT_FALSE(mDB.AddInstance(duplicateInstance).IsNone());

    // Add instance to non-existent network
    auto instanceBadNetwork = CreateInstance("service3", "subject3", 0, "nonexistent", "node1", "172.17.0.20");
    ASSERT_FALSE(mDB.AddInstance(instanceBadNetwork).IsNone());

    // Add instance to non-existent host
    auto instanceBadHost = CreateInstance("service4", "subject4", 0, "network1", "nonexistent", "172.17.0.21");
    ASSERT_FALSE(mDB.AddInstance(instanceBadHost).IsNone());

    // Verify instances
    StaticArray<networkmanager::Instance, 3> instances;
    ASSERT_TRUE(mDB.GetInstances("network1", "node1", instances).IsNone());

    EXPECT_THAT(ToVector(instances), UnorderedElementsAre(instance1, instance2, instance3));
}

TEST_F(CMDatabaseTest, NetworkManagerRemoveNetwork)
{
    ASSERT_TRUE(mDB.Init(mDatabaseConfig).IsNone());

    auto network1 = CreateNetwork("network1", "172.17.0.0/16", 1000);
    auto network2 = CreateNetwork("network2", "172.18.0.0/16", 2000);
    auto network3 = CreateNetwork("network3", "10.0.0.0/8", 3000);

    // Add networks
    ASSERT_TRUE(mDB.AddNetwork(network1).IsNone());
    ASSERT_TRUE(mDB.AddNetwork(network2).IsNone());
    ASSERT_TRUE(mDB.AddNetwork(network3).IsNone());

    // Remove network
    ASSERT_TRUE(mDB.RemoveNetwork("network2").IsNone());

    // Remove non-existent network
    ASSERT_FALSE(mDB.RemoveNetwork("nonexistent").IsNone());

    // Verify remaining networks
    StaticArray<networkmanager::Network, 2> networks;
    ASSERT_TRUE(mDB.GetNetworks(networks).IsNone());

    EXPECT_THAT(ToVector(networks), UnorderedElementsAre(network1, network3));
}

TEST_F(CMDatabaseTest, NetworkManagerRemoveHost)
{
    ASSERT_TRUE(mDB.Init(mDatabaseConfig).IsNone());

    // Create network
    auto network = CreateNetwork("network1", "172.17.0.0/16", 1000);
    ASSERT_TRUE(mDB.AddNetwork(network).IsNone());

    auto host1 = CreateHost("node1", "172.17.0.2");
    auto host2 = CreateHost("node2", "172.17.0.3");
    auto host3 = CreateHost("node3", "172.17.0.4");

    // Add hosts
    ASSERT_TRUE(mDB.AddHost("network1", host1).IsNone());
    ASSERT_TRUE(mDB.AddHost("network1", host2).IsNone());
    ASSERT_TRUE(mDB.AddHost("network1", host3).IsNone());

    // Remove host
    ASSERT_TRUE(mDB.RemoveHost("network1", "node2").IsNone());

    // Remove non-existent host
    ASSERT_FALSE(mDB.RemoveHost("network1", "nonexistent").IsNone());

    // Remove host from non-existent network
    ASSERT_FALSE(mDB.RemoveHost("nonexistent", "node1").IsNone());

    // Verify remaining hosts
    StaticArray<networkmanager::Host, 2> hosts;
    ASSERT_TRUE(mDB.GetHosts("network1", hosts).IsNone());

    EXPECT_THAT(ToVector(hosts), UnorderedElementsAre(host1, host3));
}

TEST_F(CMDatabaseTest, NetworkManagerRemoveInstance)
{
    ASSERT_TRUE(mDB.Init(mDatabaseConfig).IsNone());

    // Create network and host
    auto network = CreateNetwork("network1", "172.17.0.0/16", 1000);
    ASSERT_TRUE(mDB.AddNetwork(network).IsNone());

    auto host = CreateHost("node1", "172.17.0.2");
    ASSERT_TRUE(mDB.AddHost("network1", host).IsNone());

    auto instance1 = CreateInstance("service1", "subject1", 0, "network1", "node1", "172.17.0.10");
    auto instance2 = CreateInstance("service1", "subject1", 1, "network1", "node1", "172.17.0.11");
    auto instance3 = CreateInstance("service2", "subject2", 0, "network1", "node1", "172.17.0.12");

    // Add instances
    ASSERT_TRUE(mDB.AddInstance(instance1).IsNone());
    ASSERT_TRUE(mDB.AddInstance(instance2).IsNone());
    ASSERT_TRUE(mDB.AddInstance(instance3).IsNone());

    // Remove instance
    auto instanceIdent2 = CreateInstanceIdent("service1", "subject1", 1);
    ASSERT_TRUE(mDB.RemoveNetworkInstance(instanceIdent2).IsNone());

    // Remove non-existent instance
    auto nonExistentIdent = CreateInstanceIdent("nonexistent", "subject", 99);
    ASSERT_FALSE(mDB.RemoveNetworkInstance(nonExistentIdent).IsNone());

    // Verify remaining instances
    StaticArray<networkmanager::Instance, 2> instances;
    ASSERT_TRUE(mDB.GetInstances("network1", "node1", instances).IsNone());

    EXPECT_THAT(ToVector(instances), UnorderedElementsAre(instance1, instance3));
}

/***********************************************************************************************************************
 * launcher::StorageItf tests
 **********************************************************************************************************************/

TEST_F(CMDatabaseTest, LauncherAddInstance)
{
    ASSERT_TRUE(mDB.Init(mDatabaseConfig).IsNone());

    auto instance1 = CreateLauncherInstanceInfo("service1", "subject1", 0, "image1", "node1");
    auto instance2 = CreateLauncherInstanceInfo("service1", "subject1", 1, "image1", "node1");
    auto instance3 = CreateLauncherInstanceInfo("service2", "subject2", 0, "image2", "node2");

    // Add instances
    ASSERT_TRUE(mDB.AddInstance(instance1).IsNone());
    ASSERT_TRUE(mDB.AddInstance(instance2).IsNone());
    ASSERT_TRUE(mDB.AddInstance(instance3).IsNone());

    // Add duplicate instance
    auto duplicateInstance = CreateLauncherInstanceInfo("service1", "subject1", 0, "image99", "node99");
    ASSERT_FALSE(mDB.AddInstance(duplicateInstance).IsNone());

    // Verify instances
    StaticArray<launcher::InstanceInfo, 3> instances;
    ASSERT_TRUE(mDB.GetActiveInstances(instances).IsNone());

    EXPECT_THAT(ToVector(instances), UnorderedElementsAre(instance1, instance2, instance3));
}

TEST_F(CMDatabaseTest, LauncherUpdateInstance)
{
    ASSERT_TRUE(mDB.Init(mDatabaseConfig).IsNone());

    auto instance1 = CreateLauncherInstanceInfo("service1", "subject1", 0, "image1", "node1");
    auto instance2 = CreateLauncherInstanceInfo("service2", "subject2", 0, "image2", "node2");

    // Add instances
    ASSERT_TRUE(mDB.AddInstance(instance1).IsNone());
    ASSERT_TRUE(mDB.AddInstance(instance2).IsNone());

    // Update instance
    instance1.mImageID    = "image1-updated";
    instance1.mNodeID     = "node1-updated";
    instance1.mPrevNodeID = "node1";
    instance1.mRuntimeID  = "crun";
    instance1.mUID        = 2000;
    instance1.mCached     = false;

    ASSERT_TRUE(mDB.UpdateInstance(instance1).IsNone());

    // Update non-existent instance
    auto nonExistentInstance = CreateLauncherInstanceInfo("nonexistent", "subject", 99, "image99", "node99");
    ASSERT_FALSE(mDB.UpdateInstance(nonExistentInstance).IsNone());

    // Verify updated instance
    launcher::InstanceInfo retrievedInstance;
    ASSERT_TRUE(mDB.GetInstance(instance1.mInstanceIdent, retrievedInstance).IsNone());

    EXPECT_EQ(retrievedInstance, instance1);

    // Verify second instance was not affected
    ASSERT_TRUE(mDB.GetInstance(instance2.mInstanceIdent, retrievedInstance).IsNone());

    EXPECT_EQ(retrievedInstance, instance2);
}

TEST_F(CMDatabaseTest, LauncherGetInstance)
{
    ASSERT_TRUE(mDB.Init(mDatabaseConfig).IsNone());

    auto instance1 = CreateLauncherInstanceInfo("service1", "subject1", 0, "image1", "node1");
    auto instance2 = CreateLauncherInstanceInfo("service2", "subject2", 0, "image2", "node2");

    // Add instances
    ASSERT_TRUE(mDB.AddInstance(instance1).IsNone());
    ASSERT_TRUE(mDB.AddInstance(instance2).IsNone());

    // Get existing instances
    launcher::InstanceInfo retrievedInstance;

    ASSERT_TRUE(mDB.GetInstance(instance1.mInstanceIdent, retrievedInstance).IsNone());
    EXPECT_EQ(retrievedInstance, instance1);

    ASSERT_TRUE(mDB.GetInstance(instance2.mInstanceIdent, retrievedInstance).IsNone());
    EXPECT_EQ(retrievedInstance, instance2);

    // Get non-existent instance
    auto nonExistentIdent = CreateInstanceIdent("nonexistent", "subject", 99);
    ASSERT_FALSE(mDB.GetInstance(nonExistentIdent, retrievedInstance).IsNone());
}

TEST_F(CMDatabaseTest, LauncherGetActiveInstances)
{
    ASSERT_TRUE(mDB.Init(mDatabaseConfig).IsNone());

    // Get instances when database is empty
    StaticArray<launcher::InstanceInfo, 3> emptyInstances;
    ASSERT_TRUE(mDB.GetActiveInstances(emptyInstances).IsNone());
    EXPECT_EQ(emptyInstances.Size(), 0);

    auto instance1 = CreateLauncherInstanceInfo("service1", "subject1", 0, "image1", "node1");
    auto instance2 = CreateLauncherInstanceInfo("service2", "subject2", 0, "image2", "node2");

    // Add instances
    ASSERT_TRUE(mDB.AddInstance(instance1).IsNone());
    ASSERT_TRUE(mDB.AddInstance(instance2).IsNone());

    // Get all instances
    StaticArray<launcher::InstanceInfo, 2> instances;
    ASSERT_TRUE(mDB.GetActiveInstances(instances).IsNone());

    EXPECT_THAT(ToVector(instances), UnorderedElementsAre(instance1, instance2));
}

TEST_F(CMDatabaseTest, LauncherRemoveInstance)
{
    ASSERT_TRUE(mDB.Init(mDatabaseConfig).IsNone());

    auto instance1 = CreateLauncherInstanceInfo("service1", "subject1", 0, "image1", "node1");
    auto instance2 = CreateLauncherInstanceInfo("service2", "subject2", 0, "image2", "node2");

    ASSERT_TRUE(mDB.AddInstance(instance1).IsNone());
    ASSERT_TRUE(mDB.AddInstance(instance2).IsNone());

    // Remove instance
    ASSERT_TRUE(mDB.RemoveInstance(instance1.mInstanceIdent).IsNone());

    // Remove non-existent instance
    auto nonExistentIdent = CreateInstanceIdent("nonexistent", "subject", 99);
    ASSERT_FALSE(mDB.RemoveInstance(nonExistentIdent).IsNone());
}

} // namespace aos::cm::database
