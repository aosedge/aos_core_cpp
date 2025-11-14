/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gmock/gmock.h>

#include <core/common/tests/utils/log.hpp>

#include <sm/database/database.hpp>

using namespace testing;

/***********************************************************************************************************************
 * Constants
 **********************************************************************************************************************/

constexpr auto cCreateNullOptTime = true;

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

namespace {

aos::InstanceNetworkParameters CreateNetworkParameters()
{
    aos::InstanceNetworkParameters params;

    params.mNetworkID = "networkID";
    params.mSubnet    = "subnet";
    params.mIP        = "ip";

    params.mDNSServers.PushBack("dns1");
    params.mDNSServers.PushBack("dns2");

    for (size_t i = 0; i < 2; ++i) {
        aos::FirewallRule rule;

        const auto suffix = std::to_string(i);

        rule.mDstIP   = std::string("dstIP").append(suffix).c_str();
        rule.mDstPort = std::string("dstPort").append(suffix).c_str();
        rule.mProto   = std::string("proto").append(suffix).c_str();
        rule.mSrcIP   = std::string("srcIP").append(suffix).c_str();

        params.mFirewallRules.PushBack(Move(rule));
    }

    return params;
}

aos::InstanceIdent CreateInstanceIdent(const std::string& serviceID, const std::string& subjectID, uint32_t instance)
{
    aos::InstanceIdent ident;

    ident.mItemID    = serviceID.c_str();
    ident.mSubjectID = subjectID.c_str();
    ident.mInstance  = instance;

    return ident;
}

aos::Optional<aos::Time> CreateTimeOpt(bool createNullOpt = false)
{
    if (createNullOpt) {
        return {};
    }

    return {aos::Time::Now()};
}

aos::InstanceFilter CreateInstanceFilter(const std::string& serviceID, const std::string& subjectID, int instance)
{
    aos::InstanceFilter instanceFilter;

    if (!serviceID.empty()) {
        instanceFilter.mItemID.SetValue(serviceID.c_str());
    }

    if (!subjectID.empty()) {
        instanceFilter.mSubjectID.SetValue(subjectID.c_str());
    }

    if (instance >= 0) {
        instanceFilter.mInstance.SetValue(static_cast<uint32_t>(instance));
    }

    return instanceFilter;
}

aos::sm::launcher::InstanceData CreateInstanceData(const aos::String& id, const aos::InstanceIdent& ident)
{
    aos::sm::launcher::InstanceData instance = {};

    instance.mInstanceID                                     = id;
    static_cast<aos::InstanceIdent&>(instance.mInstanceInfo) = ident;
    instance.mInstanceInfo.mUID                              = 10;
    instance.mInstanceInfo.mPriority                         = 20;
    instance.mInstanceInfo.mStoragePath                      = "storage-path";
    instance.mInstanceInfo.mStatePath                        = "state-path";

    return instance;
}

aos::sm::servicemanager::ServiceData CreateServiceData(
    const std::string& serviceID = "service-id", const std::string& version = "0.0.1")
{
    aos::sm::servicemanager::ServiceData service;

    service.mServiceID      = serviceID.c_str();
    service.mProviderID     = "provider-id";
    service.mVersion        = version.c_str();
    service.mImagePath      = "image-path";
    service.mManifestDigest = "manifest-digest";
    service.mTimestamp      = aos::Time::Now();
    service.mState          = aos::ServiceStateEnum::eActive;
    service.mSize           = 1024;
    service.mGID            = 16;

    return service;
}

std::string GetMigrationSourceDir()
{
#ifndef DATABASE_MIGRATION_PATH
#error "DATABASE_MIGRATION_PATH must be defined"
#endif
    return std::filesystem::canonical(DATABASE_MIGRATION_PATH).string();
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

        std::filesystem::remove_all(sWorkingDir);

        mMigrationConfig.mMigrationPath.append(sWorkingDir).append("/migration");
        mMigrationConfig.mMergedMigrationPath.append(sWorkingDir).append("/merged-migration");

        std::filesystem::create_directories(mMigrationConfig.mMigrationPath);
        std::filesystem::create_directories(mMigrationConfig.mMergedMigrationPath);

        auto migrationSrc = GetMigrationSourceDir();
        auto migrationDst = std::filesystem::current_path() / mMigrationConfig.mMigrationPath;

        std::filesystem::copy(migrationSrc, migrationDst,
            std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
    }

protected:
    static constexpr auto          sWorkingDir = "database";
    aos::common::config::Migration mMigrationConfig;
    aos::sm::database::Database    mDB;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(DatabaseTest, AddInstance)
{
    ASSERT_TRUE(mDB.Init(sWorkingDir, mMigrationConfig).IsNone());

    auto networkParams = CreateNetworkParameters();
    auto instance      = CreateInstanceData("id1", CreateInstanceIdent("service", "subject", 1));

    instance.mInstanceInfo.mNetworkParameters = networkParams;

    ASSERT_TRUE(mDB.AddInstance(instance).IsNone());
    EXPECT_TRUE(mDB.AddInstance(instance).Is(aos::ErrorEnum::eFailed));
}

TEST_F(DatabaseTest, UpdateInstance)
{
    ASSERT_TRUE(mDB.Init(sWorkingDir, mMigrationConfig).IsNone());

    auto networkParams = CreateNetworkParameters();
    auto instance      = CreateInstanceData("id1", CreateInstanceIdent("service-1", "subject-1", 1));

    instance.mInstanceInfo.mNetworkParameters = networkParams;

    ASSERT_TRUE(mDB.AddInstance(instance).IsNone());

    instance.mInstanceInfo.mPriority = 100;

    ASSERT_TRUE(mDB.UpdateInstance(instance).IsNone());

    instance.mInstanceID = "unknown";

    ASSERT_TRUE(mDB.UpdateInstance(instance).Is(aos::ErrorEnum::eNotFound));
}

TEST_F(DatabaseTest, RemoveInstance)
{
    ASSERT_TRUE(mDB.Init(sWorkingDir, mMigrationConfig).IsNone());

    ASSERT_TRUE(mDB.RemoveInstance("unknown").Is(aos::ErrorEnum::eNotFound));

    auto instance = CreateInstanceData("id1", CreateInstanceIdent("service-1", "subject-1", 1));

    ASSERT_TRUE(mDB.AddInstance(instance).IsNone());

    ASSERT_TRUE(mDB.RemoveInstance("id1").IsNone());
}

TEST_F(DatabaseTest, GetAllInstances)
{
    ASSERT_TRUE(mDB.Init(sWorkingDir, mMigrationConfig).IsNone());

    auto instance = CreateInstanceData("id1", CreateInstanceIdent("service-1", "subject-1", 1));

    ASSERT_TRUE(mDB.AddInstance(instance).IsNone());

    aos::sm::launcher::InstanceDataArray result;

    ASSERT_TRUE(mDB.GetAllInstances(result).IsNone());

    ASSERT_EQ(result.Size(), 1);
    EXPECT_EQ(result[0], instance);
}

TEST_F(DatabaseTest, GetAllInstancesExceedsLimit)
{
    ASSERT_TRUE(mDB.Init(sWorkingDir, mMigrationConfig).IsNone());

    for (size_t i = 0; i < aos::cMaxNumInstances + 1; ++i) {
        auto instanceIdent = CreateInstanceIdent("service-1", "subject-1", i);
        auto instance      = CreateInstanceData(std::to_string(i).c_str(), instanceIdent);

        ASSERT_TRUE(mDB.AddInstance(instance).IsNone());
    }

    aos::sm::launcher::InstanceDataArray result;

    EXPECT_TRUE(mDB.GetAllInstances(result).Is(aos::ErrorEnum::eNoMemory));
}

TEST_F(DatabaseTest, GetOperationVersion)
{
    auto err = mDB.Init(sWorkingDir, mMigrationConfig);
    ASSERT_TRUE(err.IsNone()) << "db init failed: " << err.StrValue();

    auto result = mDB.GetOperationVersion();
    EXPECT_TRUE(result.mError.IsNone()) << "expected no error, got: " << result.mError.StrValue();
    EXPECT_EQ(result.mValue, aos::sm::launcher::Launcher::cOperationVersion);
}

TEST_F(DatabaseTest, SetOperationVersion)
{
    auto err = mDB.Init(sWorkingDir, mMigrationConfig);
    ASSERT_TRUE(err.IsNone()) << "db init failed: " << err.StrValue();

    EXPECT_TRUE(mDB.SetOperationVersion(11).IsNone());

    auto result = mDB.GetOperationVersion();
    EXPECT_TRUE(result.mError.IsNone()) << "expected no error, got: " << result.mError.StrValue();
    EXPECT_EQ(result.mValue, 11);
}

TEST_F(DatabaseTest, TablesAreDroppedIfOperationVersionMismatch)
{
    auto dbPtr = std::make_unique<aos::sm::database::Database>();

    ASSERT_TRUE(dbPtr->Init(sWorkingDir, mMigrationConfig).IsNone());

    EXPECT_TRUE(dbPtr->SetOperationVersion(11).IsNone());

    auto instance = CreateInstanceData("id1", CreateInstanceIdent("service", "subject", 1));

    EXPECT_TRUE(dbPtr->AddInstance(instance).IsNone());

    aos::Error err;
    uint64_t   dbOperationVersion = 0;

    Tie(dbOperationVersion, err) = dbPtr->GetOperationVersion();

    ASSERT_TRUE(err.IsNone());
    ASSERT_EQ(dbOperationVersion, 11);

    // Reinitialize the database with

    dbPtr = std::make_unique<aos::sm::database::Database>();

    ASSERT_TRUE(dbPtr->Init(sWorkingDir, mMigrationConfig).IsNone());

    Tie(dbOperationVersion, err) = dbPtr->GetOperationVersion();

    ASSERT_TRUE(err.IsNone());
    ASSERT_EQ(dbOperationVersion, aos::sm::launcher::Launcher::cOperationVersion);

    aos::sm::launcher::InstanceDataArray result;

    ASSERT_TRUE(dbPtr->GetAllInstances(result).IsNone());
    ASSERT_TRUE(result.IsEmpty());
}

TEST_F(DatabaseTest, OverrideEnvVars)
{
    ASSERT_TRUE(mDB.Init(sWorkingDir, mMigrationConfig).IsNone());

    aos::EnvVarInfoArray envVars;
    envVars.PushBack({"key1", "value1", CreateTimeOpt()});
    envVars.PushBack({"key2", "value2", CreateTimeOpt(cCreateNullOptTime)});

    aos::EnvVarsInstanceInfoArray envVarsInstanceInfo;
    envVarsInstanceInfo.PushBack({CreateInstanceFilter("service", "subject", 1), envVars});
    envVarsInstanceInfo.PushBack({CreateInstanceFilter("", "subject", -1), envVars});
    envVarsInstanceInfo.PushBack({CreateInstanceFilter("", "", -1), envVars});

    ASSERT_TRUE(mDB.SetOverrideEnvVars(envVarsInstanceInfo).IsNone());

    aos::EnvVarsInstanceInfoArray result;

    ASSERT_TRUE(mDB.GetOverrideEnvVars(result).IsNone());

    EXPECT_EQ(result, envVarsInstanceInfo);
}

TEST_F(DatabaseTest, SetAndGetOnlineTime)
{
    ASSERT_TRUE(mDB.Init(sWorkingDir, mMigrationConfig).IsNone());

    auto time = aos::Time::Now();

    ASSERT_TRUE(mDB.SetOnlineTime(time).IsNone());

    const auto [result, err] = mDB.GetOnlineTime();
    ASSERT_TRUE(err.IsNone()) << "get online time failed: " << err.StrValue();

    EXPECT_EQ(result, time);
}

TEST_F(DatabaseTest, AddAndGetService)
{
    ASSERT_TRUE(mDB.Init(sWorkingDir, mMigrationConfig).IsNone());

    constexpr auto serviceID = "service-1";

    auto serviceV1 = CreateServiceData(serviceID, "0.0.1");
    auto serviceV2 = CreateServiceData(serviceID, "0.0.2");

    ASSERT_TRUE(mDB.AddService(serviceV1).IsNone());
    ASSERT_TRUE(mDB.AddService(serviceV2).IsNone());

    aos::sm::servicemanager::ServiceDataArray result;

    ASSERT_TRUE(mDB.GetServiceVersions(serviceID, result).IsNone());

    ASSERT_EQ(result.Size(), 2);

    bool areEqual
        = (result[0] == serviceV1 && result[1] == serviceV2) || (result[0] == serviceV2 && result[1] == serviceV1);

    EXPECT_TRUE(areEqual);
}

TEST_F(DatabaseTest, GetServiceReturnsNotFound)
{
    ASSERT_TRUE(mDB.Init(sWorkingDir, mMigrationConfig).IsNone());

    aos::sm::servicemanager::ServiceDataArray result;

    ASSERT_TRUE(mDB.GetServiceVersions("unknown", result).Is(aos::ErrorEnum::eNotFound));
    ASSERT_TRUE(result.IsEmpty());
}

TEST_F(DatabaseTest, UpdateService)
{
    ASSERT_TRUE(mDB.Init(sWorkingDir, mMigrationConfig).IsNone());

    auto service = CreateServiceData();

    ASSERT_TRUE(mDB.AddService(service).IsNone());

    service.mGID += 1;
    service.mState     = aos::ServiceStateEnum::eActive;
    service.mTimestamp = aos::Time::Now();

    ASSERT_TRUE(mDB.UpdateService(service).IsNone());

    aos::sm::servicemanager::ServiceDataArray result;

    ASSERT_TRUE(mDB.GetServiceVersions(service.mServiceID, result).IsNone());

    ASSERT_EQ(result.Size(), 1);
    EXPECT_EQ(result[0], service);
}

TEST_F(DatabaseTest, UpdateServiceVersionFails)
{
    ASSERT_TRUE(mDB.Init(sWorkingDir, mMigrationConfig).IsNone());

    auto service = CreateServiceData();

    ASSERT_TRUE(mDB.AddService(service).IsNone());

    auto updatedService       = service;
    updatedService.mVersion   = "0.0.2";
    updatedService.mState     = aos::ServiceStateEnum::eCached;
    updatedService.mTimestamp = aos::Time::Now();

    ASSERT_FALSE(mDB.UpdateService(updatedService).IsNone());

    aos::sm::servicemanager::ServiceDataArray result;

    ASSERT_TRUE(mDB.GetServiceVersions(service.mServiceID, result).IsNone());

    ASSERT_EQ(result.Size(), 1);
    ASSERT_EQ(result[0], service);
}

TEST_F(DatabaseTest, RemoveServiceSucceeds)
{
    ASSERT_TRUE(mDB.Init(sWorkingDir, mMigrationConfig).IsNone());

    auto service = CreateServiceData();

    ASSERT_TRUE(mDB.AddService(service).IsNone());

    ASSERT_TRUE(mDB.RemoveService(service.mServiceID, service.mVersion).IsNone());

    aos::sm::servicemanager::ServiceDataArray result;

    ASSERT_TRUE(mDB.GetServiceVersions(service.mServiceID, result).Is(aos::ErrorEnum::eNotFound));

    (void)result;
}

TEST_F(DatabaseTest, GetAllServicesSucceeds)
{
    ASSERT_TRUE(mDB.Init(sWorkingDir, mMigrationConfig).IsNone());

    aos::sm::servicemanager::ServiceDataArray services;
    services.PushBack(CreateServiceData("service-1", "0.0.1"));
    services.PushBack(CreateServiceData("service-1", "0.0.2"));
    services.PushBack(CreateServiceData("service-2", "0.0.1"));

    for (const auto& service : services) {
        ASSERT_TRUE(mDB.AddService(service).IsNone());
    }

    aos::sm::servicemanager::ServiceDataArray result;

    ASSERT_TRUE(mDB.GetAllServices(result).IsNone());

    EXPECT_EQ(services, result) << "expected services are not equal to the result";
}

TEST_F(DatabaseTest, GetAllServicesExceedsApplicationLimit)
{
    ASSERT_TRUE(mDB.Init(sWorkingDir, mMigrationConfig).IsNone());

    std::vector<aos::sm::servicemanager::ServiceData> services;
    for (size_t i = 0; i < aos::cMaxNumServices + 1; ++i) {
        services.push_back(CreateServiceData(std::to_string(i), "0.0.1"));

        ASSERT_TRUE(mDB.AddService(services.back()).IsNone());
    }

    aos::sm::servicemanager::ServiceDataArray result;

    ASSERT_TRUE(mDB.GetAllServices(result).Is(aos::ErrorEnum::eNoMemory));
}

TEST_F(DatabaseTest, AddNetworkInfoSucceeds)
{
    ASSERT_TRUE(mDB.Init(sWorkingDir, mMigrationConfig).IsNone());

    aos::sm::networkmanager::NetworkInfo networkParams {"networkID", "subnet", "ip", 1, "vlanIfName"};

    ASSERT_TRUE(mDB.AddNetworkInfo(networkParams).IsNone());
    ASSERT_TRUE(mDB.AddNetworkInfo(networkParams).Is(aos::ErrorEnum::eFailed));

    ASSERT_TRUE(mDB.RemoveNetworkInfo(networkParams.mNetworkID).IsNone());
}

TEST_F(DatabaseTest, RemoveNetworkInfoReturnsNotFound)
{
    ASSERT_TRUE(mDB.Init(sWorkingDir, mMigrationConfig).IsNone());

    ASSERT_TRUE(mDB.RemoveNetworkInfo("unknown").Is(aos::ErrorEnum::eNotFound));
}

TEST_F(DatabaseTest, GetNetworksInfoSucceeds)
{
    ASSERT_TRUE(mDB.Init(sWorkingDir, mMigrationConfig).IsNone());

    aos::StaticArray<aos::sm::networkmanager::NetworkInfo, 2> networks;
    aos::StaticArray<aos::sm::networkmanager::NetworkInfo, 2> expectedNetworks;

    networks.PushBack({"networkID-1", "subnet", "ip", 1, "vlanIfName"});
    networks.PushBack({"networkID-2", "subnet", "ip", 1, "vlanIfName"});

    for (const auto& network : networks) {
        ASSERT_TRUE(mDB.AddNetworkInfo(network).IsNone());
    }

    ASSERT_TRUE(mDB.GetNetworksInfo(expectedNetworks).IsNone());

    EXPECT_EQ(networks, expectedNetworks) << "expected networks are not equal to the result";
}

TEST_F(DatabaseTest, AddInstanceNetworkInfo)
{
    ASSERT_TRUE(mDB.Init(sWorkingDir, mMigrationConfig).IsNone());

    aos::sm::networkmanager::InstanceNetworkInfo info;
    info.mInstanceID = "instance-1";
    info.mNetworkID  = "network-1";

    ASSERT_TRUE(mDB.AddInstanceNetworkInfo(info).IsNone());
    EXPECT_TRUE(mDB.AddInstanceNetworkInfo(info).Is(aos::ErrorEnum::eFailed));
}

TEST_F(DatabaseTest, RemoveInstanceNetworkInfo)
{
    ASSERT_TRUE(mDB.Init(sWorkingDir, mMigrationConfig).IsNone());

    aos::sm::networkmanager::InstanceNetworkInfo info;
    info.mInstanceID = "instance-1";
    info.mNetworkID  = "network-1";

    ASSERT_TRUE(mDB.AddInstanceNetworkInfo(info).IsNone());
    ASSERT_TRUE(mDB.RemoveInstanceNetworkInfo(info.mInstanceID).IsNone());
    EXPECT_TRUE(mDB.RemoveInstanceNetworkInfo(info.mInstanceID).Is(aos::ErrorEnum::eNotFound));
}

TEST_F(DatabaseTest, GetInstanceNetworksInfo)
{
    ASSERT_TRUE(mDB.Init(sWorkingDir, mMigrationConfig).IsNone());

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
    ASSERT_TRUE(mDB.Init(sWorkingDir, mMigrationConfig).IsNone());

    aos::Array<aos::sm::networkmanager::InstanceNetworkInfo> result;

    ASSERT_TRUE(mDB.GetInstanceNetworksInfo(result).IsNone());
    EXPECT_TRUE(result.IsEmpty());
}

TEST_F(DatabaseTest, SetUpdateAndRemoveTrafficMonitorDataSucceeds)
{
    ASSERT_TRUE(mDB.Init(sWorkingDir, mMigrationConfig).IsNone());

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

TEST_F(DatabaseTest, AddLayer)
{
    ASSERT_TRUE(mDB.Init(sWorkingDir, mMigrationConfig).IsNone());

    aos::sm::layermanager::LayerData layer;
    layer.mLayerID = "layerID";

    ASSERT_TRUE(mDB.AddLayer(layer).IsNone());
    ASSERT_TRUE(mDB.AddLayer(layer).Is(aos::ErrorEnum::eFailed));
}

TEST_F(DatabaseTest, GetAllLayers)
{
    ASSERT_TRUE(mDB.Init(sWorkingDir, mMigrationConfig).IsNone());

    aos::StaticArray<aos::sm::layermanager::LayerData, 2> layers;
    layers.PushBack({"digest-1", "unpacked-1", "layerID-1", "path-1", "osVersion-1", "version-1", aos::Time::Now(),
        aos::LayerStateEnum::eCached, 1024});
    layers.PushBack({"digest-2", "unpacked-2", "layerID-2", "path-2", "osVersion-2", "version-2", aos::Time::Now(),
        aos::LayerStateEnum::eActive, 2048});

    for (const auto& layer : layers) {
        ASSERT_TRUE(mDB.AddLayer(layer).IsNone());
    }

    aos::StaticArray<aos::sm::layermanager::LayerData, 2> result;

    ASSERT_TRUE(mDB.GetAllLayers(result).IsNone());

    ASSERT_EQ(layers, result) << "expected layers are not equal to the result";

    for (const auto& layer : result) {
        ASSERT_TRUE(mDB.RemoveLayer(layer.mLayerDigest).IsNone());
    }

    result.Clear();

    ASSERT_TRUE(mDB.GetAllLayers(result).IsNone());

    EXPECT_TRUE(result.IsEmpty()) << "expected empty result";
}

TEST_F(DatabaseTest, GetLayerSucceeds)
{
    ASSERT_TRUE(mDB.Init(sWorkingDir, mMigrationConfig).IsNone());

    aos::sm::layermanager::LayerData layer {"digest-1", "unpacked-digest", "layerID-1", "path-1", "osVersion-1",
        "version-1", aos::Time::Now(), aos::LayerStateEnum::eActive, 1024};

    ASSERT_TRUE(mDB.AddLayer(layer).IsNone());

    aos::sm::layermanager::LayerData dbLayer;

    ASSERT_TRUE(mDB.GetLayer(layer.mLayerDigest, dbLayer).IsNone());

    ASSERT_EQ(dbLayer, layer);

    layer.mLayerID   = "layerID-2";
    layer.mTimestamp = aos::Time::Now();

    ASSERT_TRUE(mDB.UpdateLayer(layer).IsNone());

    ASSERT_TRUE(mDB.GetLayer(layer.mLayerDigest, dbLayer).IsNone());

    EXPECT_EQ(dbLayer, layer);
}

TEST_F(DatabaseTest, GetLayerReturnsNotFound)
{
    ASSERT_TRUE(mDB.Init(sWorkingDir, mMigrationConfig).IsNone());

    aos::sm::layermanager::LayerData layer;

    EXPECT_TRUE(mDB.GetLayer("unknown", layer).Is(aos::ErrorEnum::eNotFound));
}

TEST_F(DatabaseTest, UpdateLayerReturnsNotFound)
{
    ASSERT_TRUE(mDB.Init(sWorkingDir, mMigrationConfig).IsNone());

    aos::sm::layermanager::LayerData layer {"digest-1", "unpacked-1", "layerID-1", "path-1", "osVersion-1", "version-1",
        aos::Time::Now(), aos::LayerStateEnum::eActive, 1024};

    EXPECT_TRUE(mDB.UpdateLayer(layer).Is(aos::ErrorEnum::eNotFound));
}

TEST_F(DatabaseTest, JournalCursor)
{
    ASSERT_TRUE(mDB.Init(sWorkingDir, mMigrationConfig).IsNone());

    aos::StaticString<32> journalCursor;

    ASSERT_TRUE(mDB.GetJournalCursor(journalCursor).IsNone());
    EXPECT_TRUE(journalCursor.IsEmpty());

    ASSERT_TRUE(mDB.SetJournalCursor("cursor").IsNone());

    ASSERT_TRUE(mDB.GetJournalCursor(journalCursor).IsNone());
    EXPECT_EQ(journalCursor, aos::String("cursor"));
}

TEST_F(DatabaseTest, GetInstanceInfoByIDOk)
{
    ASSERT_TRUE(mDB.Init(sWorkingDir, mMigrationConfig).IsNone());

    constexpr auto serviceID  = "service-1";
    constexpr auto instanceID = "instance-1";

    auto serviceV1 = CreateServiceData(serviceID, "0.0.1");
    auto serviceV2 = CreateServiceData(serviceID, "0.0.2");

    ASSERT_TRUE(mDB.AddService(serviceV1).IsNone());
    ASSERT_TRUE(mDB.AddService(serviceV2).IsNone());

    auto instance = CreateInstanceData(instanceID, CreateInstanceIdent(serviceID, "subject", 0));

    EXPECT_TRUE(mDB.AddInstance(instance).IsNone());

    const aos::sm::alerts::ServiceInstanceData cExpServiceInfo = {{serviceID, "subject", 0}, "0.0.2"};

    auto [serviceInstanceInfo, err] = mDB.GetInstanceInfoByID(instanceID);

    EXPECT_EQ(cExpServiceInfo, serviceInstanceInfo);
    EXPECT_TRUE(err.IsNone());
}

TEST_F(DatabaseTest, GetInstanceIDsOk)
{
    ASSERT_TRUE(mDB.Init(sWorkingDir, mMigrationConfig).IsNone());

    constexpr auto serviceID   = "service-1";
    constexpr auto instanceID  = "instance-1";
    constexpr auto instanceID2 = "instance-2";

    auto instance1 = CreateInstanceData(instanceID, CreateInstanceIdent(serviceID, "subject", 0));
    auto instance2 = CreateInstanceData(instanceID2, CreateInstanceIdent(serviceID, "subject", 1));

    EXPECT_TRUE(mDB.AddInstance(instance1).IsNone());
    EXPECT_TRUE(mDB.AddInstance(instance2).IsNone());

    aos::InstanceFilter filter;

    filter.mItemID.SetValue(serviceID);
    filter.mSubjectID.SetValue("subject");

    auto [instanceIDs, err] = mDB.GetInstanceIDs(filter);
    EXPECT_TRUE(err.IsNone());
    EXPECT_THAT(instanceIDs, ElementsAre(instanceID, instanceID2));
}

TEST_F(DatabaseTest, GetInstanceIDsNOK)
{
    ASSERT_TRUE(mDB.Init(sWorkingDir, mMigrationConfig).IsNone());

    constexpr auto serviceID = "service-1";

    aos::InstanceFilter filter;

    filter.mItemID.SetValue(serviceID);

    auto [instanceIDs, err] = mDB.GetInstanceIDs(filter);
    EXPECT_FALSE(err.IsNone());
}
