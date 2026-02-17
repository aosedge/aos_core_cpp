/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gmock/gmock.h>

#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>

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

InstanceIdent CreateInstanceIdent(const char* itemID, const char* subjectID, uint64_t instance,
    UpdateItemType itemType = UpdateItemTypeEnum::eService, bool preinstalled = true)
{
    InstanceIdent ident;

    ident.mItemID       = itemID;
    ident.mSubjectID    = subjectID;
    ident.mInstance     = instance;
    ident.mType         = itemType;
    ident.mPreinstalled = preinstalled;

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
    const char* networkID, const char* nodeID, const char* ip, UpdateItemType itemType = UpdateItemTypeEnum::eService)
{
    networkmanager::Instance inst;

    inst.mInstanceIdent = CreateInstanceIdent(itemID, subjectID, instance, itemType);
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

launcher::InstanceInfo CreateLauncherInstanceInfo(const char* itemID, const char* subjectID, uint64_t instance,
    const char* manifestDigest, const char* nodeID, UpdateItemType itemType = UpdateItemTypeEnum::eService,
    launcher::InstanceStateEnum state = launcher::InstanceStateEnum::eCached, bool isUnitSubject = false,
    const char* version = "1.0.0", const char* ownerID = "owner1", SubjectTypeEnum subjectType = SubjectTypeEnum::eUser,
    size_t priority = 0, std::vector<const char*> labels = {})
{
    launcher::InstanceInfo info;

    info.mInstanceIdent  = CreateInstanceIdent(itemID, subjectID, instance, itemType);
    info.mManifestDigest = manifestDigest;
    info.mNodeID         = nodeID;
    info.mPrevNodeID     = "prevNode";
    info.mRuntimeID      = "runc";
    info.mUID            = 1000;
    info.mGID            = 2000;
    info.mTimestamp      = Time::Now();
    info.mState          = state;
    info.mIsUnitSubject  = isUnitSubject;
    info.mVersion        = version;
    info.mOwnerID        = ownerID;
    info.mSubjectType    = subjectType;
    info.mLabels.Clear();
    for (const char* label : labels) {
        AOS_ERROR_CHECK_AND_THROW(info.mLabels.EmplaceBack(label), "can't add label");
    }
    info.mPriority = priority;

    return info;
}

imagemanager::ItemInfo CreateImageManagerItemInfo(
    const char* itemID, const UpdateItemType& type, const char* version, const char* indexDigest, ItemState state)
{
    imagemanager::ItemInfo info;

    info.mItemID      = itemID;
    info.mType        = type;
    info.mVersion     = version;
    info.mIndexDigest = indexDigest;
    info.mState       = state;
    info.mTimestamp   = Time::Now();

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
    static void SetUpTestSuite() { tests::utils::InitLog(); }

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
    auto instance3
        = CreateInstance("service2", "subject2", 0, "network1", "node1", "172.17.0.12", UpdateItemTypeEnum::eComponent);

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

    auto instance1
        = CreateLauncherInstanceInfo("service1", "subject1", 0, "image1", "node1", UpdateItemTypeEnum::eService,
            launcher::InstanceStateEnum::eActive, false, "1.0.0", "owner1", SubjectTypeEnum::eUser, 50, {"label1"});
    auto instance2
        = CreateLauncherInstanceInfo("service1", "subject1", 1, "image1", "node1", UpdateItemTypeEnum::eService,
            launcher::InstanceStateEnum::eCached, true, "1.0.0", "owner1", SubjectTypeEnum::eUser, 75);
    auto instance3 = CreateLauncherInstanceInfo("service2", "subject2", 0, "image2", "node2",
        UpdateItemTypeEnum::eComponent, launcher::InstanceStateEnum::eDisabled, false, "2.0.0", "owner1",
        SubjectTypeEnum::eUser, 100, {"label2", "label3"});

    // Add instances
    ASSERT_TRUE(mDB.AddInstance(instance1).IsNone());
    ASSERT_TRUE(mDB.AddInstance(instance2).IsNone());
    ASSERT_TRUE(mDB.AddInstance(instance3).IsNone());

    // Add duplicate instance (same primary key including version)
    auto duplicateInstance = CreateLauncherInstanceInfo("service1", "subject1", 0, "image99", "node99",
        UpdateItemTypeEnum::eService, launcher::InstanceStateEnum::eCached, false, "1.0.0");
    ASSERT_FALSE(mDB.AddInstance(duplicateInstance).IsNone());

    // Add instance with same InstanceIdent but different version (should succeed)
    auto instance1v2 = CreateLauncherInstanceInfo("service1", "subject1", 0, "image1", "node1",
        UpdateItemTypeEnum::eService, launcher::InstanceStateEnum::eActive, false, "2.0.0");
    ASSERT_TRUE(mDB.AddInstance(instance1v2).IsNone());

    // Verify instances
    StaticArray<launcher::InstanceInfo, 4> instances;
    ASSERT_TRUE(mDB.GetActiveInstances(instances).IsNone());

    EXPECT_THAT(ToVector(instances), UnorderedElementsAre(instance1, instance2, instance3, instance1v2));
}

TEST_F(CMDatabaseTest, LauncherUpdateInstance)
{
    ASSERT_TRUE(mDB.Init(mDatabaseConfig).IsNone());

    auto instance1 = CreateLauncherInstanceInfo("service1", "subject1", 0, "image1", "node1",
        UpdateItemTypeEnum::eService, launcher::InstanceStateEnum::eCached, false, "1.0.0");
    auto instance2 = CreateLauncherInstanceInfo("service2", "subject2", 0, "image2", "node2",
        UpdateItemTypeEnum::eService, launcher::InstanceStateEnum::eActive, true, "1.0.0");

    // Add instances
    ASSERT_TRUE(mDB.AddInstance(instance1).IsNone());
    ASSERT_TRUE(mDB.AddInstance(instance2).IsNone());

    // Update instance
    instance1.mManifestDigest = "image1-updated";
    instance1.mNodeID         = "node1-updated";
    instance1.mPrevNodeID     = "node1";
    instance1.mRuntimeID      = "crun";
    instance1.mUID            = 2000;
    instance1.mState          = launcher::InstanceStateEnum::eActive;
    instance1.mIsUnitSubject  = true;
    instance1.mLabels.Clear();
    AOS_ERROR_CHECK_AND_THROW(instance1.mLabels.EmplaceBack("label1"), "can't add label");
    AOS_ERROR_CHECK_AND_THROW(instance1.mLabels.EmplaceBack("label2"), "can't add label");
    instance1.mPriority = 100;

    ASSERT_TRUE(mDB.UpdateInstance(instance1).IsNone());

    // Update non-existent instance
    auto nonExistentInstance = CreateLauncherInstanceInfo("nonexistent", "subject", 99, "image99", "node99",
        UpdateItemTypeEnum::eService, launcher::InstanceStateEnum::eCached, false, "1.0.0");
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

    auto instance1 = CreateLauncherInstanceInfo("service1", "subject1", 0, "image1", "node1",
        UpdateItemTypeEnum::eService, launcher::InstanceStateEnum::eActive, false, "1.0.0", "owner1",
        SubjectTypeEnum::eUser, 25, {"label1", "label2"});
    auto instance2
        = CreateLauncherInstanceInfo("service2", "subject2", 0, "image2", "node2", UpdateItemTypeEnum::eService,
            launcher::InstanceStateEnum::eCached, true, "2.0.0", "owner2", SubjectTypeEnum::eUser, 150, {"label3"});

    // Add instances
    ASSERT_TRUE(mDB.AddInstance(instance1).IsNone());
    ASSERT_TRUE(mDB.AddInstance(instance2).IsNone());

    // Get existing instances
    launcher::InstanceInfo retrievedInstance;

    ASSERT_TRUE(mDB.GetInstance(instance1.mInstanceIdent, retrievedInstance).IsNone());
    EXPECT_EQ(retrievedInstance, instance1);
    EXPECT_EQ(retrievedInstance.mVersion, "1.0.0");

    ASSERT_TRUE(mDB.GetInstance(instance2.mInstanceIdent, retrievedInstance).IsNone());
    EXPECT_EQ(retrievedInstance, instance2);
    EXPECT_EQ(retrievedInstance.mVersion, "2.0.0");

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

    auto instance1
        = CreateLauncherInstanceInfo("service1", "subject1", 0, "image1", "node1", UpdateItemTypeEnum::eService,
            launcher::InstanceStateEnum::eActive, false, "1.0.0", "owner1", SubjectTypeEnum::eUser, 80, {"label4"});
    auto instance2 = CreateLauncherInstanceInfo("service2", "subject2", 0, "image2", "node2",
        UpdateItemTypeEnum::eService, launcher::InstanceStateEnum::eDisabled, true, "2.0.0", "owner2",
        SubjectTypeEnum::eUser, 200, {"label5", "label6", "label7"});

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

    auto instance1
        = CreateLauncherInstanceInfo("service1", "subject1", 0, "image1", "node1", UpdateItemTypeEnum::eService,
            launcher::InstanceStateEnum::eCached, true, "1.0.0", "owner1", SubjectTypeEnum::eUser, 30, {"label8"});
    auto instance2 = CreateLauncherInstanceInfo("service2", "subject2", 0, "image2", "node2",
        UpdateItemTypeEnum::eService, launcher::InstanceStateEnum::eActive, false, "2.0.0", "owner2",
        SubjectTypeEnum::eUser, 175, {"label9", "label10"});

    ASSERT_TRUE(mDB.AddInstance(instance1).IsNone());
    ASSERT_TRUE(mDB.AddInstance(instance2).IsNone());

    // Remove instance
    ASSERT_TRUE(mDB.RemoveInstance(instance1.mInstanceIdent).IsNone());

    // Remove non-existent instance
    auto nonExistentIdent = CreateInstanceIdent("nonexistent", "subject", 99);
    ASSERT_FALSE(mDB.RemoveInstance(nonExistentIdent).IsNone());
}

/***********************************************************************************************************************
 * imagemanager::StorageItf tests
 **********************************************************************************************************************/

TEST_F(CMDatabaseTest, ImageManagerAddItem)
{
    ASSERT_TRUE(mDB.Init(mDatabaseConfig).IsNone());

    auto item1 = CreateImageManagerItemInfo(
        "service1", UpdateItemTypeEnum::eService, "1.0.0", "sha256:abc123", ItemStateEnum::eInstalled);
    auto item2 = CreateImageManagerItemInfo(
        "service1", UpdateItemTypeEnum::eService, "2.0.0", "sha256:def456", ItemStateEnum::eInstalled);
    auto item3 = CreateImageManagerItemInfo(
        "service2", UpdateItemTypeEnum::eService, "1.0.0", "sha256:ghi789", ItemStateEnum::ePending);

    ASSERT_TRUE(mDB.AddItem(item1).IsNone());
    ASSERT_TRUE(mDB.AddItem(item2).IsNone());
    ASSERT_TRUE(mDB.AddItem(item3).IsNone());

    auto duplicateItem = CreateImageManagerItemInfo(
        "service1", UpdateItemTypeEnum::eService, "1.0.0", "sha256:xyz999", ItemStateEnum::eInstalled);
    ASSERT_FALSE(mDB.AddItem(duplicateItem).IsNone());

    StaticArray<imagemanager::ItemInfo, 3> items;

    ASSERT_TRUE(mDB.GetAllItemsInfos(items).IsNone());

    EXPECT_THAT(ToVector(items), UnorderedElementsAre(item1, item2, item3));
}

TEST_F(CMDatabaseTest, ImageManagerRemoveItem)
{
    ASSERT_TRUE(mDB.Init(mDatabaseConfig).IsNone());

    auto item1 = CreateImageManagerItemInfo(
        "service1", UpdateItemTypeEnum::eService, "1.0.0", "sha256:abc123", ItemStateEnum::eInstalled);
    auto item2 = CreateImageManagerItemInfo(
        "service1", UpdateItemTypeEnum::eService, "2.0.0", "sha256:def456", ItemStateEnum::eInstalled);
    auto item3 = CreateImageManagerItemInfo(
        "service2", UpdateItemTypeEnum::eService, "1.0.0", "sha256:ghi789", ItemStateEnum::ePending);

    ASSERT_TRUE(mDB.AddItem(item1).IsNone());
    ASSERT_TRUE(mDB.AddItem(item2).IsNone());
    ASSERT_TRUE(mDB.AddItem(item3).IsNone());

    ASSERT_TRUE(mDB.RemoveItem("service1", "1.0.0").IsNone());

    ASSERT_FALSE(mDB.RemoveItem("nonexistent", "1.0.0").IsNone());

    StaticArray<imagemanager::ItemInfo, 2> items;

    ASSERT_TRUE(mDB.GetAllItemsInfos(items).IsNone());

    EXPECT_THAT(ToVector(items), UnorderedElementsAre(item2, item3));
}

TEST_F(CMDatabaseTest, ImageManagerUpdateItemState)
{
    ASSERT_TRUE(mDB.Init(mDatabaseConfig).IsNone());

    auto item1 = CreateImageManagerItemInfo(
        "service1", UpdateItemTypeEnum::eService, "1.0.0", "sha256:abc123", ItemStateEnum::ePending);
    auto item2 = CreateImageManagerItemInfo(
        "service2", UpdateItemTypeEnum::eService, "1.0.0", "sha256:def456", ItemStateEnum::ePending);

    ASSERT_TRUE(mDB.AddItem(item1).IsNone());
    ASSERT_TRUE(mDB.AddItem(item2).IsNone());

    auto newTimestamp = Time::Now();
    ASSERT_TRUE(mDB.UpdateItemState("service1", "1.0.0", ItemStateEnum::eInstalled, newTimestamp).IsNone());

    ASSERT_FALSE(mDB.UpdateItemState("nonexistent", "1.0.0", ItemStateEnum::eInstalled).IsNone());

    StaticArray<imagemanager::ItemInfo, 1> items;

    ASSERT_TRUE(mDB.GetItemInfos("service1", items).IsNone());
    ASSERT_EQ(items.Size(), 1);
    EXPECT_EQ(items[0].mState, ItemStateEnum::eInstalled);
    EXPECT_EQ(items[0].mTimestamp, newTimestamp);

    StaticArray<imagemanager::ItemInfo, 1> items2;

    ASSERT_TRUE(mDB.GetItemInfos("service2", items2).IsNone());
    ASSERT_EQ(items2.Size(), 1);
    EXPECT_EQ(items2[0].mState, ItemStateEnum::ePending);
}

TEST_F(CMDatabaseTest, ImageManagerGetItemsInfo)
{
    ASSERT_TRUE(mDB.Init(mDatabaseConfig).IsNone());

    StaticArray<imagemanager::ItemInfo, 3> emptyItems;

    ASSERT_TRUE(mDB.GetAllItemsInfos(emptyItems).IsNone());
    EXPECT_EQ(emptyItems.Size(), 0);

    auto item1 = CreateImageManagerItemInfo(
        "service1", UpdateItemTypeEnum::eService, "1.0.0", "sha256:abc123", ItemStateEnum::eInstalled);
    auto item2 = CreateImageManagerItemInfo(
        "service1", UpdateItemTypeEnum::eService, "2.0.0", "sha256:def456", ItemStateEnum::eInstalled);
    auto item3 = CreateImageManagerItemInfo(
        "service2", UpdateItemTypeEnum::eService, "1.0.0", "sha256:ghi789", ItemStateEnum::ePending);

    ASSERT_TRUE(mDB.AddItem(item1).IsNone());
    ASSERT_TRUE(mDB.AddItem(item2).IsNone());
    ASSERT_TRUE(mDB.AddItem(item3).IsNone());

    StaticArray<imagemanager::ItemInfo, 3> items;

    ASSERT_TRUE(mDB.GetAllItemsInfos(items).IsNone());

    EXPECT_THAT(ToVector(items), UnorderedElementsAre(item1, item2, item3));
}

TEST_F(CMDatabaseTest, ImageManagerGetItemsInfos)
{
    ASSERT_TRUE(mDB.Init(mDatabaseConfig).IsNone());

    auto item1 = CreateImageManagerItemInfo(
        "service1", UpdateItemTypeEnum::eService, "1.0.0", "sha256:abc123", ItemStateEnum::eInstalled);
    auto item2 = CreateImageManagerItemInfo(
        "service1", UpdateItemTypeEnum::eService, "2.0.0", "sha256:def456", ItemStateEnum::eInstalled);
    auto item3 = CreateImageManagerItemInfo(
        "service2", UpdateItemTypeEnum::eService, "1.0.0", "sha256:ghi789", ItemStateEnum::ePending);

    ASSERT_TRUE(mDB.AddItem(item1).IsNone());
    ASSERT_TRUE(mDB.AddItem(item2).IsNone());
    ASSERT_TRUE(mDB.AddItem(item3).IsNone());

    StaticArray<imagemanager::ItemInfo, 2> service1Items;

    ASSERT_TRUE(mDB.GetItemInfos("service1", service1Items).IsNone());
    EXPECT_THAT(ToVector(service1Items), UnorderedElementsAre(item1, item2));

    StaticArray<imagemanager::ItemInfo, 1> service2Items;

    ASSERT_TRUE(mDB.GetItemInfos("service2", service2Items).IsNone());
    EXPECT_THAT(ToVector(service2Items), UnorderedElementsAre(item3));

    StaticArray<imagemanager::ItemInfo, 1> nonExistentItems;

    ASSERT_TRUE(mDB.GetItemInfos("nonexistent", nonExistentItems).IsNone());
    EXPECT_EQ(nonExistentItems.Size(), 0);
}

/***********************************************************************************************************************
 * updatemanager::StorageItf tests
 **********************************************************************************************************************/

TEST_F(CMDatabaseTest, StoreGetUpdateState)
{
    ASSERT_TRUE(mDB.Init(mDatabaseConfig).IsNone());

    ASSERT_TRUE(mDB.StoreUpdateState(updatemanager::UpdateStateEnum::eDownloading).IsNone());
    ASSERT_TRUE(mDB.StoreUpdateState(updatemanager::UpdateStateEnum::ePending).IsNone());
    ASSERT_TRUE(mDB.StoreUpdateState(updatemanager::UpdateStateEnum::eInstalling).IsNone());

    auto [state, err] = mDB.GetUpdateState();
    ASSERT_TRUE(err.IsNone());

    EXPECT_EQ(state, updatemanager::UpdateStateEnum::eInstalling);
}

TEST_F(CMDatabaseTest, StoreGetDesiredStatus)
{
    ASSERT_TRUE(mDB.Init(mDatabaseConfig).IsNone());

    auto setDesiredStatus = std::make_unique<DesiredStatus>();

    // Nodes
    setDesiredStatus->mNodes.EmplaceBack();
    setDesiredStatus->mNodes.Back().mNodeID = "node1";
    setDesiredStatus->mNodes.Back().mState  = DesiredNodeStateEnum::eProvisioned;
    setDesiredStatus->mNodes.EmplaceBack();
    setDesiredStatus->mNodes.Back().mNodeID = "node2";
    setDesiredStatus->mNodes.Back().mState  = DesiredNodeStateEnum::ePaused;
    // UnitConfig
    setDesiredStatus->mUnitConfig.EmplaceValue();
    auto& unitConfig          = *setDesiredStatus->mUnitConfig;
    unitConfig.mFormatVersion = "1.0.0";
    unitConfig.mVersion       = "2.0.0";
    unitConfig.mNodes.EmplaceBack();
    auto& nodeConfig1     = unitConfig.mNodes.Back();
    nodeConfig1.mNodeID   = "node1";
    nodeConfig1.mNodeType = "main";
    nodeConfig1.mPriority = 5;
    unitConfig.mNodes.EmplaceBack();
    auto& nodeConfig2     = unitConfig.mNodes.Back();
    nodeConfig2.mNodeID   = "node2";
    nodeConfig2.mNodeType = "secondary";
    nodeConfig2.mPriority = 10;
    // Items
    setDesiredStatus->mUpdateItems.EmplaceBack();
    auto& updateItem1        = setDesiredStatus->mUpdateItems.Back();
    updateItem1.mItemID      = "item1";
    updateItem1.mType        = UpdateItemTypeEnum::eService;
    updateItem1.mVersion     = "1.0.0";
    updateItem1.mOwnerID     = "owner1";
    updateItem1.mIndexDigest = "sha256:abcdef";
    setDesiredStatus->mUpdateItems.EmplaceBack();
    auto& updateItem2        = setDesiredStatus->mUpdateItems.Back();
    updateItem2.mItemID      = "item2";
    updateItem2.mType        = UpdateItemTypeEnum::eComponent;
    updateItem2.mVersion     = "2.0.0";
    updateItem2.mOwnerID     = "owner2";
    updateItem2.mIndexDigest = "sha256:123456";
    // Instances
    setDesiredStatus->mInstances.EmplaceBack();
    auto& instance1         = setDesiredStatus->mInstances.Back();
    instance1.mItemID       = "item1";
    instance1.mSubjectID    = "subject1";
    instance1.mPriority     = 1;
    instance1.mNumInstances = 2;
    instance1.mLabels.PushBack("main");
    setDesiredStatus->mInstances.EmplaceBack();
    auto& instance2      = setDesiredStatus->mInstances.Back();
    instance2.mItemID    = "item2";
    instance2.mSubjectID = "subject2";
    instance2.mPriority  = 5;
    // Subjects
    setDesiredStatus->mSubjects.EmplaceBack();
    auto& subject1          = setDesiredStatus->mSubjects.Back();
    subject1.mSubjectID     = "subject1";
    subject1.mSubjectType   = SubjectTypeEnum::eUser;
    subject1.mIsUnitSubject = true;
    setDesiredStatus->mSubjects.EmplaceBack();
    auto& subject2          = setDesiredStatus->mSubjects.Back();
    subject2.mSubjectID     = "subject2";
    subject2.mSubjectType   = SubjectTypeEnum::eGroup;
    subject2.mIsUnitSubject = false;
    // Certificates
    setDesiredStatus->mCertificates.EmplaceBack();
    auto& certificate1        = setDesiredStatus->mCertificates.Back();
    certificate1.mCertificate = String("der certificate example").AsByteArray();
    certificate1.mFingerprint = "fingerprint1";
    setDesiredStatus->mCertificates.EmplaceBack();
    auto& certificate2        = setDesiredStatus->mCertificates.Back();
    certificate2.mCertificate = String("another der certificate").AsByteArray();
    certificate2.mFingerprint = "fingerprint2";
    // Certificate chains
    setDesiredStatus->mCertificateChains.EmplaceBack();
    auto& certificateChain1 = setDesiredStatus->mCertificateChains.Back();
    certificateChain1.mName = "chain1";
    certificateChain1.mFingerprints.PushBack("fingerprint1");
    certificateChain1.mFingerprints.PushBack("fingerprint2");
    setDesiredStatus->mCertificateChains.EmplaceBack();
    auto& certificateChain2 = setDesiredStatus->mCertificateChains.Back();
    certificateChain2.mName = "chain2";
    certificateChain2.mFingerprints.PushBack("fingerprint3");

    auto getDesiredStatus = std::make_unique<DesiredStatus>();

    ASSERT_TRUE(mDB.StoreDesiredStatus(*setDesiredStatus).IsNone());

    auto err = mDB.GetDesiredStatus(*getDesiredStatus);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(*getDesiredStatus, *setDesiredStatus);
}

} // namespace aos::cm::database
