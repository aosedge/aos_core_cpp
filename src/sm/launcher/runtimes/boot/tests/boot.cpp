/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>
#include <future>
#include <vector>

#include <gtest/gtest.h>

#include <core/common/tests/mocks/currentnodeinfoprovidermock.hpp>
#include <core/common/tests/mocks/ocispecmock.hpp>
#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>
#include <core/sm/tests/mocks/instancestatusreceivermock.hpp>
#include <core/sm/tests/mocks/iteminfoprovidermock.hpp>
#include <core/sm/tests/mocks/rebootermock.hpp>
#include <core/sm/tests/mocks/updatecheckermock.hpp>
#include <core/sm/tests/stubs/instancestatusreceiver.hpp>

#include <common/tests/utils/partition.hpp>
#include <common/utils/exception.hpp>
#include <common/utils/time.hpp>
#include <common/utils/utils.hpp>

#include <sm/launcher/runtimes/rootfs/rootfs.hpp>
#include <sm/tests/mocks/systemdconnmock.hpp>

#include <sm/launcher/runtimes/boot/boot.hpp>
#include <sm/launcher/runtimes/boot/eficontroller.hpp>
#include <sm/launcher/runtimes/boot/partitionmanager.hpp>

#include "partitionmanagermock.hpp"

using namespace testing;

namespace aos::sm::launcher {

namespace {

/***********************************************************************************************************************
 * Consts
 **********************************************************************************************************************/

const auto     cTestDir                = std::filesystem::absolute("testBoot");
const auto     cWorkingDir             = cTestDir / "workdir";
const auto     cBootRuntimeWorkingDir  = cWorkingDir / "runtimes/boot";
const auto     cInstalledInstance      = cBootRuntimeWorkingDir / "installed.json";
const auto     cPendingInstance        = cBootRuntimeWorkingDir / "pending.json";
const auto     cBootPartitionMountDir  = cBootRuntimeWorkingDir / "mnt";
const auto     cTestDisk               = cTestDir / "disk";
const auto     cPartition1             = cTestDisk / "1";
const auto     cPartition2             = cTestDisk / "2";
const auto     cUpdateImage            = cBootRuntimeWorkingDir / "images" / "boot.img";
const auto     cUpdateImageArchivePath = cTestDir / "boot.img.gz";
constexpr auto cRuntimeID              = "ddb944db-faba-39d9-9982-8be46f10293b";

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

class MockBootController : public BootControllerItf {
public:
    MOCK_METHOD(Error, Init, (const BootConfig& config), (override));
    MOCK_METHOD(Error, GetPartitionDevices, (std::vector<std::string>&), (const, override));
    MOCK_METHOD(RetWithError<size_t>, GetCurrentBoot, (), (const, override));
    MOCK_METHOD(RetWithError<size_t>, GetMainBoot, (), (const, override));
    MOCK_METHOD(Error, SetMainBoot, (size_t index), (override));
    MOCK_METHOD(Error, SetBootOK, (), (override));
};

class TestBootRuntime : public BootRuntime {
public:
    TestBootRuntime(
        std::shared_ptr<PartitionManagerMock> partitionManager, std::shared_ptr<MockBootController> mockBootController)
        : mPartitionManager(partitionManager)
        , mMockBootController(mockBootController)
    {
    }

private:
    std::shared_ptr<PartitionManagerItf> CreatePartitionManager() const override { return mPartitionManager; }
    std::shared_ptr<BootControllerItf>   CreateBootController() const override { return mMockBootController; }

    std::shared_ptr<PartitionManagerMock> mPartitionManager;
    std::shared_ptr<MockBootController>   mMockBootController;
};

} // namespace

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class BootRuntimeTest : public Test {
protected:
    static void SetUpTestSuite() { tests::utils::InitLog(); }

    void SetUp() override
    {
        std::error_code ec;
        std::filesystem::remove_all(cWorkingDir, ec);
        std::filesystem::remove_all(cTestDir, ec);

        std::filesystem::create_directories(cBootRuntimeWorkingDir);
        std::filesystem::create_directories(cTestDisk);

        WriteVersionFiles();
        InitConfig();
        InitNodeInfo();

        mBootAPartition.mDevice = cPartition1.string();
        mBootBPartition.mDevice = cPartition2.string();

        EXPECT_CALL(*mMockBootController, GetPartitionDevices)
            .WillRepeatedly(
                DoAll(SetArgReferee<0>(std::vector<std::string> {cPartition1.string(), cPartition2.string()}),
                    Return(ErrorEnum::eNone)));

        EXPECT_CALL(*mMockBootController, Init).WillRepeatedly(Return(ErrorEnum::eNone));
    }

    void TearDown() override { }

    void WriteVersionFiles()
    {
        {
            std::filesystem::create_directory(cPartition1);

            std::ofstream file(cPartition1 / "version.txt");
            file << R"(VERSION="1.0.0")" << std::endl;
        }

        {
            std::filesystem::create_directory(cPartition2);

            std::ofstream file(cPartition2 / "version.txt");
            file << R"(VERSION="1.0.1")" << std::endl;
        }
    }

    void CheckVersionFileContent(const std::filesystem::path& partitionPath, const std::string& expectedVersion)
    {
        std::string version;

        {
            std::ifstream versionFile(partitionPath / "version.txt");
            ASSERT_TRUE(versionFile.is_open()) << "Can't open version file";

            std::string line;
            std::getline(versionFile, line);
            version = line;
        }

        EXPECT_EQ(version, R"(VERSION=")" + expectedVersion + R"(")");
    }

    void CreateUpdateImageArchive(const std::filesystem::path& partitionPath)
    {
        auto res = common::utils::ExecCommand({"gzip", "--keep", (partitionPath / "version.txt").string()});
        ASSERT_TRUE(res.mError.IsNone()) << tests::utils::ErrorToStr(res.mError);

        std::filesystem::copy_file(partitionPath / "version.txt.gz", cUpdateImageArchivePath,
            std::filesystem::copy_options::overwrite_existing);
    }

    void InitConfig()
    {
        mConfig.mWorkingDir = cWorkingDir.string();
        mConfig.mType       = cRuntimeBoot;
        mConfig.mConfig     = Poco::makeShared<Poco::JSON::Object>();

        mConfig.mConfig->set("versionFile", "version.txt");
        mConfig.mConfig->set("partitions", Poco::makeShared<Poco::JSON::Array>());

        for (const auto& partition : {"a", "b"}) {
            mConfig.mConfig->getArray("partitions")->add(partition);
        }
    }

    void InitNodeInfo()
    {
        mNodeInfo.mNodeID = "node1";

        EXPECT_CALL(mCurrentNodeInfoProvider, GetCurrentNodeInfo)
            .WillRepeatedly(DoAll(SetArgReferee<0>(mNodeInfo), Return(ErrorEnum::eNone)));
    }

    PartInfo                               mBootAPartition;
    PartInfo                               mBootBPartition;
    NodeInfo                               mNodeInfo;
    RuntimeConfig                          mConfig;
    iamclient::CurrentNodeInfoProviderMock mCurrentNodeInfoProvider;
    imagemanager::ItemInfoProviderMock     mItemInfoProvider;
    oci::OCISpecMock                       mOCISpec;
    InstanceStatusReceiverStub             mStatusReceiver;
    sm::utils::SystemdConnMock             mSystemdConn;
    std::shared_ptr<PartitionManagerMock>  mPartitionManager {std::make_shared<PartitionManagerMock>()};
    std::shared_ptr<MockBootController>    mMockBootController {std::make_shared<MockBootController>()};
    TestBootRuntime                        mBootRuntime {mPartitionManager, mMockBootController};
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(BootRuntimeTest, GetRuntimeInfo)
{
    EXPECT_CALL(*mMockBootController, GetCurrentBoot).WillOnce(Return(0u));
    EXPECT_CALL(*mMockBootController, GetMainBoot).WillOnce(Return(0u));
    EXPECT_CALL(*mMockBootController, SetBootOK).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mPartitionManager, GetPartInfo(cPartition1.string(), _))
        .WillOnce(DoAll(SetArgReferee<1>(mBootAPartition), Return(ErrorEnum::eNone)));
    EXPECT_CALL(*mPartitionManager, Mount(mBootAPartition, cBootPartitionMountDir.string(), _))
        .WillOnce(Invoke([](const PartInfo&, const std::string&, int) {
            std::filesystem::create_directory(cBootPartitionMountDir);

            std::filesystem::copy_file(cPartition1 / "version.txt", cBootPartitionMountDir / "version.txt",
                std::filesystem::copy_options::overwrite_existing);

            return ErrorEnum::eNone;
        }));
    EXPECT_CALL(*mPartitionManager, Unmount(cBootPartitionMountDir.string())).WillOnce(Return(ErrorEnum::eNone));

    auto err = mBootRuntime.Init(
        mConfig, mCurrentNodeInfoProvider, mItemInfoProvider, mOCISpec, mStatusReceiver, mSystemdConn);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = mBootRuntime.Start();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    auto info = std::make_unique<RuntimeInfo>();

    err = mBootRuntime.GetRuntimeInfo(*info);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_STREQ(info->mRuntimeType.CStr(), cRuntimeBoot);
    EXPECT_EQ(info->mMaxInstances, 1u);
    EXPECT_STREQ(info->mRuntimeID.CStr(), cRuntimeID);

    err = mBootRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(BootRuntimeTest, PreInstalledStatusIsSentOnStart)
{
    EXPECT_CALL(*mMockBootController, GetCurrentBoot).WillOnce(Return(0u));
    EXPECT_CALL(*mMockBootController, GetMainBoot).WillOnce(Return(0u));
    EXPECT_CALL(*mMockBootController, SetBootOK).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mPartitionManager, GetPartInfo(cPartition1.string(), _))
        .WillOnce(DoAll(SetArgReferee<1>(mBootAPartition), Return(ErrorEnum::eNone)));
    EXPECT_CALL(*mPartitionManager, Mount(mBootAPartition, cBootPartitionMountDir.string(), _))
        .WillOnce(Invoke([](const PartInfo&, const std::string&, int) {
            std::filesystem::create_directory(cBootPartitionMountDir);

            std::filesystem::copy_file(cPartition1 / "version.txt", cBootPartitionMountDir / "version.txt",
                std::filesystem::copy_options::overwrite_existing);

            return ErrorEnum::eNone;
        }));
    EXPECT_CALL(*mPartitionManager, Unmount(cBootPartitionMountDir.string())).WillOnce(Return(ErrorEnum::eNone));

    auto err = mBootRuntime.Init(
        mConfig, mCurrentNodeInfoProvider, mItemInfoProvider, mOCISpec, mStatusReceiver, mSystemdConn);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = mBootRuntime.Start();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    std::vector<InstanceStatus> statuses;

    err = mStatusReceiver.GetStatuses(statuses, std::chrono::seconds(1));
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    ASSERT_EQ(statuses.size(), 1u);

    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eActive);
    EXPECT_STREQ(statuses[0].mVersion.CStr(), "1.0.0");
    EXPECT_STREQ(statuses[0].mManifestDigest.CStr(), "");
    EXPECT_STREQ(statuses[0].mItemID.CStr(), "");
    EXPECT_STREQ(statuses[0].mSubjectID.CStr(), "");
    EXPECT_EQ(statuses[0].mInstance, 0u);
    EXPECT_TRUE(statuses[0].mPreinstalled);

    err = mBootRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(BootRuntimeTest, InstalledStatusIsSentOnStart)
{
    constexpr auto cInstalled = R"({
        "itemId": "item1",
        "subjectId": "subject1",
        "instance": 1,
        "manifestDigest": "digest",
        "state": "active",
        "version": "1.0.0",
        "partitionIndex": 0
    })";

    {
        std::ofstream file(cInstalledInstance);
        if (!file.is_open()) {
            FAIL() << "Failed to create installed instance file";
        }

        file << cInstalled;
    }

    EXPECT_CALL(*mMockBootController, GetCurrentBoot).WillOnce(Return(0u));
    EXPECT_CALL(*mMockBootController, GetMainBoot).WillOnce(Return(0u));
    EXPECT_CALL(*mMockBootController, SetBootOK).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mPartitionManager, GetPartInfo(cPartition1.string(), _))
        .WillOnce(DoAll(SetArgReferee<1>(mBootAPartition), Return(ErrorEnum::eNone)));
    EXPECT_CALL(*mPartitionManager, Mount(mBootAPartition, cBootPartitionMountDir.string(), _))
        .WillOnce(Invoke([](const PartInfo&, const std::string&, int) {
            std::filesystem::create_directory(cBootPartitionMountDir);

            std::filesystem::copy_file(cPartition1 / "version.txt", cBootPartitionMountDir / "version.txt",
                std::filesystem::copy_options::overwrite_existing);

            return ErrorEnum::eNone;
        }));
    EXPECT_CALL(*mPartitionManager, Unmount(cBootPartitionMountDir.string())).WillOnce(Return(ErrorEnum::eNone));

    auto err = mBootRuntime.Init(
        mConfig, mCurrentNodeInfoProvider, mItemInfoProvider, mOCISpec, mStatusReceiver, mSystemdConn);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = mBootRuntime.Start();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    std::vector<InstanceStatus> statuses;

    err = mStatusReceiver.GetStatuses(statuses, std::chrono::seconds(1));
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    ASSERT_EQ(statuses.size(), 1u);

    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eActive);
    EXPECT_STREQ(statuses[0].mVersion.CStr(), "1.0.0");
    EXPECT_STREQ(statuses[0].mManifestDigest.CStr(), "digest");
    EXPECT_STREQ(statuses[0].mItemID.CStr(), "item1");
    EXPECT_STREQ(statuses[0].mSubjectID.CStr(), "subject1");
    EXPECT_EQ(statuses[0].mInstance, 1u);
    EXPECT_FALSE(statuses[0].mPreinstalled);

    err = mBootRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(BootRuntimeTest, UpdateSucceededOnStart)
{
    constexpr auto cInstalled = R"({
        "manifestDigest": "preinstalledDigest",
        "state": "active",
        "version": "1.0.0",
        "partitionIndex": 0
    })";
    constexpr auto cPending   = R"({
        "itemId": "updateItem1",
        "subjectId": "updateSubject1",
        "instance": 1,
        "manifestDigest": "updateDigest",
        "state": "active",
        "partitionIndex": 1
    })";

    {
        std::ofstream file(cInstalledInstance);
        file << cInstalled;
    }

    {
        std::ofstream file(cPendingInstance);
        file << cPending;
    }

    EXPECT_CALL(*mMockBootController, GetCurrentBoot).WillOnce(Return(1u));
    EXPECT_CALL(*mMockBootController, GetMainBoot).WillOnce(Return(1u));
    EXPECT_CALL(*mMockBootController, SetBootOK).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mPartitionManager, GetPartInfo(cPartition2.string(), _))
        .WillOnce(DoAll(SetArgReferee<1>(mBootBPartition), Return(ErrorEnum::eNone)));
    EXPECT_CALL(*mPartitionManager, Mount(mBootBPartition, cBootPartitionMountDir.string(), _))
        .WillOnce(Invoke([](const PartInfo&, const std::string&, int) {
            std::filesystem::create_directory(cBootPartitionMountDir);

            std::filesystem::copy_file(cPartition2 / "version.txt", cBootPartitionMountDir / "version.txt",
                std::filesystem::copy_options::overwrite_existing);

            return ErrorEnum::eNone;
        }));
    EXPECT_CALL(*mPartitionManager, Unmount(cBootPartitionMountDir.string())).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mPartitionManager, CopyDevice(cPartition2.string(), cPartition1.string()))
        .WillOnce(Invoke([](const std::string& from, const std::string& to) {
            std::filesystem::copy_file(
                from + "/version.txt", to + "/version.txt", std::filesystem::copy_options::overwrite_existing);

            return ErrorEnum::eNone;
        }));

    auto err = mBootRuntime.Init(
        mConfig, mCurrentNodeInfoProvider, mItemInfoProvider, mOCISpec, mStatusReceiver, mSystemdConn);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = mBootRuntime.Start();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    std::vector<InstanceStatus> statuses;

    err = mStatusReceiver.GetStatuses(statuses, std::chrono::seconds(1));
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    ASSERT_EQ(statuses.size(), 2u);

    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eInactive);
    EXPECT_STREQ(statuses[0].mManifestDigest.CStr(), "preinstalledDigest");
    EXPECT_STREQ(statuses[0].mItemID.CStr(), "");
    EXPECT_STREQ(statuses[0].mSubjectID.CStr(), "");
    EXPECT_EQ(statuses[0].mInstance, 0u);
    EXPECT_EQ(statuses[0].mVersion, "1.0.0");
    EXPECT_TRUE(statuses[0].mPreinstalled);

    EXPECT_EQ(statuses[1].mState, InstanceStateEnum::eActive);
    EXPECT_STREQ(statuses[1].mManifestDigest.CStr(), "updateDigest");
    EXPECT_STREQ(statuses[1].mItemID.CStr(), "updateItem1");
    EXPECT_STREQ(statuses[1].mSubjectID.CStr(), "updateSubject1");
    EXPECT_EQ(statuses[1].mInstance, 1u);
    EXPECT_EQ(statuses[1].mVersion, "1.0.1");
    EXPECT_FALSE(statuses[1].mPreinstalled);

    for (const auto& partition : {cPartition1, cPartition2}) {
        CheckVersionFileContent(partition, "1.0.1");
    }

    err = mBootRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(BootRuntimeTest, UpdateFailedOnStart)
{
    constexpr auto cInstalled = R"({
        "manifestDigest": "preinstalledDigest",
        "state": "active",
        "version": "1.0.0",
        "partitionIndex": 0
    })";
    constexpr auto cPending   = R"({
        "itemId": "updateItem1",
        "subjectId": "updateSubject1",
        "instance": 1,
        "manifestDigest": "updateDigest",
        "state": "failed",
        "version": "1.0.1",
        "partitionIndex": 1
    })";

    {
        std::ofstream file(cInstalledInstance);
        file << cInstalled;
    }

    {
        std::ofstream file(cPendingInstance);
        file << cPending;
    }

    EXPECT_CALL(*mMockBootController, GetCurrentBoot).WillOnce(Return(0u));
    EXPECT_CALL(*mMockBootController, GetMainBoot).WillOnce(Return(1u));
    EXPECT_CALL(*mMockBootController, SetBootOK).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mPartitionManager, GetPartInfo(cPartition1.string(), _))
        .WillOnce(DoAll(SetArgReferee<1>(mBootAPartition), Return(ErrorEnum::eNone)));
    EXPECT_CALL(*mPartitionManager, Mount(mBootAPartition, cBootPartitionMountDir.string(), _))
        .WillOnce(Invoke([](const PartInfo&, const std::string&, int) {
            std::filesystem::create_directory(cBootPartitionMountDir);

            std::filesystem::copy_file(cPartition1 / "version.txt", cBootPartitionMountDir / "version.txt",
                std::filesystem::copy_options::overwrite_existing);

            return ErrorEnum::eNone;
        }));
    EXPECT_CALL(*mPartitionManager, Unmount(cBootPartitionMountDir.string())).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mPartitionManager, CopyDevice(cPartition1.string(), cPartition2.string()))
        .WillOnce(Invoke([](const std::string& from, const std::string& to) {
            std::filesystem::copy_file(
                from + "/version.txt", to + "/version.txt", std::filesystem::copy_options::overwrite_existing);

            return ErrorEnum::eNone;
        }));

    auto err = mBootRuntime.Init(
        mConfig, mCurrentNodeInfoProvider, mItemInfoProvider, mOCISpec, mStatusReceiver, mSystemdConn);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = mBootRuntime.Start();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    std::vector<InstanceStatus> statuses;

    err = mStatusReceiver.GetStatuses(statuses, std::chrono::seconds(1));
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    ASSERT_EQ(statuses.size(), 2u);

    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eFailed);
    EXPECT_STREQ(statuses[0].mManifestDigest.CStr(), "updateDigest");
    EXPECT_STREQ(statuses[0].mItemID.CStr(), "updateItem1");
    EXPECT_STREQ(statuses[0].mSubjectID.CStr(), "updateSubject1");
    EXPECT_EQ(statuses[0].mInstance, 1u);
    EXPECT_STREQ(statuses[0].mVersion.CStr(), "1.0.1");
    EXPECT_FALSE(statuses[0].mPreinstalled);

    EXPECT_EQ(statuses[1].mState, InstanceStateEnum::eActive);
    EXPECT_STREQ(statuses[1].mManifestDigest.CStr(), "preinstalledDigest");
    EXPECT_STREQ(statuses[1].mItemID.CStr(), "");
    EXPECT_STREQ(statuses[1].mSubjectID.CStr(), "");
    EXPECT_EQ(statuses[1].mInstance, 0u);
    EXPECT_STREQ(statuses[1].mVersion.CStr(), "1.0.0");
    EXPECT_TRUE(statuses[1].mPreinstalled);

    for (const auto& partition : {cPartition1, cPartition2}) {
        CheckVersionFileContent(partition, "1.0.0");
    }

    err = mBootRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(BootRuntimeTest, StartInstance)
{
    const auto cManifestPath = String("oci/manifest.json");
    const auto cLayerDigest  = String("layerDigest");

    EXPECT_CALL(*mMockBootController, GetCurrentBoot).WillOnce(Return(0u));
    EXPECT_CALL(*mMockBootController, GetMainBoot).WillOnce(Return(0u));
    EXPECT_CALL(*mMockBootController, SetBootOK).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mPartitionManager, GetPartInfo(cPartition1.string(), _))
        .WillOnce(DoAll(SetArgReferee<1>(mBootAPartition), Return(ErrorEnum::eNone)));
    EXPECT_CALL(*mPartitionManager, Mount(mBootAPartition, cBootPartitionMountDir.string(), _))
        .WillOnce(Invoke([](const PartInfo&, const std::string&, int) {
            std::filesystem::create_directory(cBootPartitionMountDir);

            std::filesystem::copy_file(cPartition1 / "version.txt", cBootPartitionMountDir / "version.txt",
                std::filesystem::copy_options::overwrite_existing);

            return ErrorEnum::eNone;
        }));
    EXPECT_CALL(*mPartitionManager, Unmount(cBootPartitionMountDir.string())).WillOnce(Return(ErrorEnum::eNone));

    auto err = mBootRuntime.Init(
        mConfig, mCurrentNodeInfoProvider, mItemInfoProvider, mOCISpec, mStatusReceiver, mSystemdConn);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = mBootRuntime.Start();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    auto instance             = std::make_unique<InstanceInfo>();
    instance->mManifestDigest = "updateDigest";
    instance->mItemID         = "item1";
    instance->mSubjectID      = "subject1";
    instance->mInstance       = 1;
    instance->mType           = UpdateItemTypeEnum::eComponent;

    EXPECT_CALL(mItemInfoProvider, GetBlobPath(instance->mManifestDigest, _))
        .WillOnce(DoAll(SetArgReferee<1>(cManifestPath), Return(ErrorEnum::eNone)));
    EXPECT_CALL(mOCISpec, LoadImageManifest(cManifestPath, _))
        .WillOnce(Invoke([cLayerDigest](const String&, aos::oci::ImageManifest& manifest) {
            manifest.mLayers.EmplaceBack();
            manifest.mLayers.Back().mDigest = cLayerDigest;

            return ErrorEnum::eNone;
        }));
    EXPECT_CALL(mItemInfoProvider, GetBlobPath(cLayerDigest, _)).WillOnce(Invoke([&](const String&, String& path) {
        CreateUpdateImageArchive(cPartition2);

        path = cUpdateImageArchivePath.c_str();
        return ErrorEnum::eNone;
    }));
    EXPECT_CALL(*mMockBootController, SetMainBoot(1)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(*mPartitionManager, InstallImage(cUpdateImage.c_str(), mBootBPartition.mDevice))
        .WillOnce(Invoke([](const std::string& from, const std::string& to) {
            LOG_DBG() << "Installing image from " << from.c_str() << " to " << to.c_str();

            std::filesystem::copy_file(from, to + "/version.txt", std::filesystem::copy_options::overwrite_existing);

            return ErrorEnum::eNone;
        }));

    auto status = std::make_unique<InstanceStatus>();

    err = mBootRuntime.StartInstance(*instance, *status);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(status->mState, InstanceStateEnum::eActivating);
    EXPECT_TRUE(static_cast<const InstanceIdent&>(*instance) == static_cast<const InstanceIdent&>(*status));

    std::vector<StaticString<cIDLen>> runtimesToReboot;

    err = mStatusReceiver.GetRuntimesToReboot(runtimesToReboot, std::chrono::seconds(1));
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    ASSERT_EQ(runtimesToReboot.size(), 1u);
    EXPECT_STREQ(runtimesToReboot[0].CStr(), cRuntimeID);

    err = mBootRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

} // namespace aos::sm::launcher
