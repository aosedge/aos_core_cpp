/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>
#include <future>

#include <Poco/JSON/Object.h>
#include <Poco/Process.h>
#include <Poco/UUIDGenerator.h>

#include <gtest/gtest.h>

#include <core/common/tests/mocks/currentnodeinfoprovidermock.hpp>
#include <core/common/tests/mocks/ocispecmock.hpp>
#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>
#include <core/sm/tests/mocks/iteminfoprovidermock.hpp>
#include <core/sm/tests/mocks/launchermock.hpp>
#include <core/sm/tests/mocks/rebootermock.hpp>
#include <core/sm/tests/mocks/updatecheckermock.hpp>
#include <core/sm/tests/stubs/instancestatusreceiver.hpp>

#include <common/utils/exception.hpp>
#include <common/utils/time.hpp>

#include <sm/launcher/runtimes/rootfs/rootfs.hpp>
#include <sm/tests/mocks/systemdconnmock.hpp>

using namespace testing;

namespace aos {

std::ostream& operator<<(std::ostream& os, const String& str)
{
    return os << str.CStr();
}

std::ostream& operator<<(std::ostream& os, const InstanceStatus& info)
{
    return os << info.mItemID << ":" << info.mSubjectID << ":" << info.mInstance << info.mPreinstalled << ":"
              << info.mNodeID << ":" << info.mRuntimeID << ":" << info.mManifestDigest << ":" << info.mVersion;
}

} // namespace aos

namespace aos::sm::launcher {

namespace {

/***********************************************************************************************************************
 * Consts
 **********************************************************************************************************************/

const auto cTestDir                  = std::filesystem::path("testRootfs");
const auto cUncompressedTestFile     = cTestDir / "testfile.1.0.1.squashfs";
const auto cWorkingDir               = cTestDir / "workdir";
const auto cInstanceFile             = cWorkingDir / "installed_instance.json";
const auto cUpdateInstanceFile       = cWorkingDir / "pending_instance.json";
const auto cVersionFile              = cTestDir / "version.txt";
const auto cUpdateRootfsManifestFile = cTestDir / "manifest.json";
const auto cUpdateRootfsFile         = cTestDir / "rootfs.1.0.1.gz";

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

void CreateGZIP(const String& path)
{
    if (std::ofstream f(cUncompressedTestFile); f.is_open()) {
        f << "This is a test file for gzip compression.";
    } else {
        throw std::runtime_error("Failed to create temporary test file");
    }

    std::vector<std::string> args = {"czf", path.CStr(), "-C", cUncompressedTestFile.parent_path().string(),
        cUncompressedTestFile.filename().string()};
    Poco::ProcessHandle      ph   = Poco::Process::launch("tar", args);
    int                      rc   = ph.wait();

    if (rc != 0) {
        throw std::runtime_error("Failed to create tar archive");
    }
}

} // namespace

class RootfsRuntimeTest : public Test {
protected:
    static void SetUpTestSuite() { tests::utils::InitLog(); }

    void SetUp() override
    {
        std::filesystem::remove_all(cTestDir);

        std::filesystem::create_directories(cWorkingDir);

        mConfig.isComponent = true;
        mConfig.mPlugin     = "rootfs";
        mConfig.mType       = "rootfs";

        {
            auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

            json->set("workingDir", cWorkingDir.string());
            json->set("versionFilePath", cVersionFile.string());
            json->set("healthCheckServices", Poco::makeShared<Poco::JSON::Array>());

            json->getArray("healthCheckServices")->add("sm");

            mConfig.mConfig = json;
        }

        EXPECT_CALL(mSystemdConn, StartUnit).WillRepeatedly(Return(ErrorEnum::eNone));

        EXPECT_CALL(mSystemdConn, GetUnitStatus("sm")).WillRepeatedly(Invoke([](const auto&) {
            sm::utils::UnitStatus status;

            status.mName        = "sm";
            status.mActiveState = sm::utils::UnitStateEnum::eActive;

            return RetWithError<sm::utils::UnitStatus>(status, ErrorEnum::eNone);
        }));

        WriteFiles();

        EXPECT_CALL(mCurrentNodeInfoProvider, GetCurrentNodeInfo(_)).WillRepeatedly(Invoke([](NodeInfo& nodeInfo) {
            nodeInfo.mNodeID   = "nodeId";
            nodeInfo.mNodeType = "nodeType";

            return ErrorEnum::eNone;
        }));
    }

    std::string GetExpectedRuntimeID() const
    {
        return Poco::UUIDGenerator::defaultGenerator().createFromName(Poco::UUID::oid(), "rootfs-nodeId").toString();
    }

    void WriteFiles()
    {
        if (std::ofstream file(cInstanceFile); file.is_open()) {
            file << R"({
                "itemId": "itemId",
                "subjectId": "subjectId",
                "manifestDigest": "manifestDigest",
                "version": "1.0.0"
            })";
        } else {
            throw std::runtime_error("can't create instance file");
        }

        if (std::ofstream file(cVersionFile); file.is_open()) {
            file << R"(VERSION="1.0.0")";
        } else {
            throw std::runtime_error("can't create version file");
        }

        if (std::ofstream file(cUpdateRootfsFile); file.is_open()) {
            file << "dummy rootfs content";
        } else {
            throw std::runtime_error("can't create rootfs file");
        }

        if (std::ofstream file(cUpdateInstanceFile); file.is_open()) {
            file << R"({
                "itemId": "updateItemId",
                "subjectId": "updateSubjectId",
                "manifestDigest": "updateManifestDigest",
                "version": "1.0.1"
            })";
        } else {
            throw std::runtime_error("can't create manifest file");
        }
    }

    RuntimeConfig                          mConfig;
    iamclient::CurrentNodeInfoProviderMock mCurrentNodeInfoProvider;
    imagemanager::ItemInfoProviderMock     mItemInfoProvider;
    oci::OCISpecMock                       mOCISpec;
    InstanceStatusReceiverStub             mStatusReceiver;
    sm::utils::SystemdConnMock             mSystemdConn;
    RootfsRuntime                          mRootfsRuntime;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(RootfsRuntimeTest, GetRuntimeInfo)
{
    auto err = mRootfsRuntime.Init(
        mConfig, mCurrentNodeInfoProvider, mItemInfoProvider, mOCISpec, mStatusReceiver, mSystemdConn);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = mRootfsRuntime.Start();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    auto info = std::make_unique<RuntimeInfo>();

    err = mRootfsRuntime.GetRuntimeInfo(*info);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_STREQ(info->mRuntimeType.CStr(), "rootfs");
    EXPECT_EQ(info->mMaxInstances, 1u);
    EXPECT_STREQ(info->mRuntimeID.CStr(), GetExpectedRuntimeID().c_str());

    err = mRootfsRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(RootfsRuntimeTest, StartInstance)
{
    const String cUpdateLayerDigest = "updateRootfsDigest";
    const String cBlobPath          = "rootfsImageDigest";

    CreateGZIP(cUpdateRootfsFile.c_str());

    auto err = mRootfsRuntime.Init(
        mConfig, mCurrentNodeInfoProvider, mItemInfoProvider, mOCISpec, mStatusReceiver, mSystemdConn);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = mRootfsRuntime.Start();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    auto instanceInfo             = std::make_unique<InstanceInfo>();
    instanceInfo->mManifestDigest = cBlobPath;
    instanceInfo->mItemID         = "itemId";
    instanceInfo->mSubjectID      = "subjectId";

    auto status = std::make_unique<InstanceStatus>();

    EXPECT_CALL(mOCISpec, LoadImageManifest(_, _))
        .WillOnce(Invoke([cUpdateLayerDigest](const String&, oci::ImageManifest& manifest) {
            manifest.mLayers.Resize(1);
            manifest.mLayers[0].mDigest    = cUpdateLayerDigest;
            manifest.mLayers[0].mMediaType = "vnd.aos.image.component.full.v1+gzip";

            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(mItemInfoProvider, GetBlobPath(cBlobPath, _))
        .WillOnce(DoAll(SetArgReferee<1>(String(cUpdateRootfsManifestFile.c_str())), Return(ErrorEnum::eNone)));

    EXPECT_CALL(mItemInfoProvider, GetBlobPath(cUpdateLayerDigest, _))
        .WillOnce(DoAll(SetArgReferee<1>(String(cUpdateRootfsFile.c_str())), Return(ErrorEnum::eNone)));

    err = mRootfsRuntime.StartInstance(*instanceInfo, *status);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(status->mState, InstanceStateEnum::eActivating) << status->mState.ToString().CStr();

    EXPECT_TRUE(std::filesystem::exists(cWorkingDir / "do_update"));

    std::vector<StaticString<cIDLen>> rebootRuntimes;

    err = mStatusReceiver.GetRuntimesToReboot(rebootRuntimes, std::chrono::seconds(1));
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    ASSERT_EQ(rebootRuntimes.size(), 1u);
    EXPECT_STREQ(rebootRuntimes[0].CStr(), GetExpectedRuntimeID().c_str());

    err = mRootfsRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(RootfsRuntimeTest, StartPreinstalledInstance)
{
    fs::RemoveAll(cInstanceFile.c_str());
    fs::RemoveAll(cUpdateInstanceFile.c_str());

    auto err = mRootfsRuntime.Init(
        mConfig, mCurrentNodeInfoProvider, mItemInfoProvider, mOCISpec, mStatusReceiver, mSystemdConn);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = mRootfsRuntime.Start();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    std::vector<InstanceStatus> onStartStatuses;

    err = mStatusReceiver.GetStatuses(onStartStatuses, std::chrono::seconds(1));
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    ASSERT_EQ(onStartStatuses.size(), 1u);
    EXPECT_EQ(onStartStatuses[0].mState, InstanceStateEnum::eActive);
    EXPECT_STREQ(onStartStatuses[0].mItemID.CStr(), "rootfs");
    EXPECT_STREQ(onStartStatuses[0].mSubjectID.CStr(), "nodeType");
    EXPECT_STREQ(onStartStatuses[0].mVersion.CStr(), "1.0.0");
    EXPECT_TRUE(onStartStatuses[0].mPreinstalled);

    std::vector<InstanceStatus> onStartInstanceStatuses;
    auto                        status = std::make_unique<InstanceStatus>();

    auto instance                          = std::make_unique<InstanceInfo>();
    static_cast<InstanceIdent&>(*instance) = static_cast<const InstanceIdent&>(onStartStatuses[0]);
    instance->mVersion                     = onStartStatuses[0].mVersion;

    err = mRootfsRuntime.StartInstance(*instance, *status);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
    EXPECT_EQ(onStartStatuses[0], *status);

    err = mStatusReceiver.GetStatuses(onStartInstanceStatuses, std::chrono::seconds(1));
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
    EXPECT_EQ(onStartStatuses, onStartInstanceStatuses);

    err = mRootfsRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(RootfsRuntimeTest, StartInstanceLoadImageManifestFailed)
{
    auto err = mRootfsRuntime.Init(
        mConfig, mCurrentNodeInfoProvider, mItemInfoProvider, mOCISpec, mStatusReceiver, mSystemdConn);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = mRootfsRuntime.Start();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    auto instanceInfo             = std::make_unique<InstanceInfo>();
    instanceInfo->mManifestDigest = "newDigest";
    instanceInfo->mItemID         = "itemId";
    instanceInfo->mSubjectID      = "subjectId";

    auto status = std::make_unique<InstanceStatus>();

    EXPECT_CALL(mOCISpec, LoadImageManifest(_, _)).WillOnce(Return(ErrorEnum::eInvalidChecksum));

    err = mRootfsRuntime.StartInstance(*instanceInfo, *status);
    ASSERT_TRUE(err.Is(ErrorEnum::eInvalidChecksum)) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(status->mState, InstanceStateEnum::eFailed) << status->mState.ToString().CStr();

    for (const auto& entry : std::filesystem::directory_iterator(cWorkingDir)) {
        EXPECT_EQ(cInstanceFile, entry.path());
    }

    err = mRootfsRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(RootfsRuntimeTest, NoPendingUpdates)
{
    const std::vector cExpectedFiles {cInstanceFile};

    std::filesystem::remove(cUpdateInstanceFile);

    auto err = mRootfsRuntime.Init(
        mConfig, mCurrentNodeInfoProvider, mItemInfoProvider, mOCISpec, mStatusReceiver, mSystemdConn);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = mRootfsRuntime.Start();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    std::vector<InstanceStatus> statuses;

    err = mStatusReceiver.GetStatuses(statuses, std::chrono::seconds(1));
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eActive);
    EXPECT_STREQ(statuses[0].mItemID.CStr(), "itemId");
    EXPECT_STREQ(statuses[0].mSubjectID.CStr(), "subjectId");
    EXPECT_STREQ(statuses[0].mManifestDigest.CStr(), "manifestDigest");
    EXPECT_STREQ(statuses[0].mVersion.CStr(), "1.0.0");

    for (const auto& entry : std::filesystem::directory_iterator(cWorkingDir)) {
        EXPECT_TRUE(std::find(cExpectedFiles.begin(), cExpectedFiles.end(), entry.path()) != cExpectedFiles.end())
            << "Unexpected file: " << entry.path();
    }

    err = mRootfsRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(RootfsRuntimeTest, UpdateIsCompleted)
{
    const std::vector cExpectedFiles {cInstanceFile};

    if (std::ofstream file(cWorkingDir / "rootfs.1.0.1.squashfs"); file.is_open()) {
        file << "1.0.1";
    } else {
        FAIL() << "can't create image file";
    }

    if (std::ofstream file(cVersionFile); file.is_open()) {
        file << R"(VERSION="1.0.1")";
    } else {
        throw std::runtime_error("can't create version file");
    }

    auto err = mRootfsRuntime.Init(
        mConfig, mCurrentNodeInfoProvider, mItemInfoProvider, mOCISpec, mStatusReceiver, mSystemdConn);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = mRootfsRuntime.Start();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    std::vector<InstanceStatus> statuses;

    err = mStatusReceiver.GetStatuses(statuses, std::chrono::seconds(1));
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eActive);
    EXPECT_STREQ(statuses[0].mItemID.CStr(), "updateItemId");
    EXPECT_STREQ(statuses[0].mSubjectID.CStr(), "updateSubjectId");
    EXPECT_STREQ(statuses[0].mManifestDigest.CStr(), "updateManifestDigest");
    EXPECT_STREQ(statuses[0].mVersion.CStr(), "1.0.1");

    for (const auto& entry : std::filesystem::directory_iterator(cWorkingDir)) {
        EXPECT_TRUE(std::find(cExpectedFiles.begin(), cExpectedFiles.end(), entry.path()) != cExpectedFiles.end())
            << "Unexpected file: " << entry.path();
    }

    err = mRootfsRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(RootfsRuntimeTest, UpdatedFailed)
{
    const std::vector cExpectedFiles {cInstanceFile, cUpdateInstanceFile, cWorkingDir / "rootfs.1.0.1.squashfs",
        cWorkingDir / "updated", cWorkingDir / "failed"};

    if (std::ofstream file(cWorkingDir / "updated"); file.is_open()) {
    } else {
        FAIL() << "can't create updated file";
    }

    if (std::ofstream file(cWorkingDir / "rootfs.1.0.1.squashfs"); file.is_open()) {
        file << "1.0.1";
    } else {
        FAIL() << "can't create image file";
    }

    if (std::ofstream file(cVersionFile); file.is_open()) {
        file << R"(VERSION="1.0.1")";
    } else {
        throw std::runtime_error("can't create version file");
    }

    auto err = mRootfsRuntime.Init(
        mConfig, mCurrentNodeInfoProvider, mItemInfoProvider, mOCISpec, mStatusReceiver, mSystemdConn);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    std::promise<void> getUnitStatusPromise;

    EXPECT_CALL(mSystemdConn, GetUnitStatus("sm")).WillOnce(Invoke([&](const auto&) {
        getUnitStatusPromise.get_future();

        sm::utils::UnitStatus status;

        status.mName        = "sm";
        status.mActiveState = sm::utils::UnitStateEnum::eFailed;

        return RetWithError<sm::utils::UnitStatus>(status, ErrorEnum::eNone);
    }));

    err = mRootfsRuntime.Start();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    std::vector<InstanceStatus> statuses;

    err = mStatusReceiver.GetStatuses(statuses, std::chrono::seconds(2));
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eActivating);
    EXPECT_STREQ(statuses[0].mItemID.CStr(), "updateItemId");
    EXPECT_STREQ(statuses[0].mSubjectID.CStr(), "updateSubjectId");
    EXPECT_STREQ(statuses[0].mManifestDigest.CStr(), "updateManifestDigest");
    EXPECT_STREQ(statuses[0].mVersion.CStr(), "1.0.1");

    getUnitStatusPromise.set_value();

    err = mStatusReceiver.GetStatuses(statuses, std::chrono::seconds(2));
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eFailed);
    EXPECT_STREQ(statuses[0].mItemID.CStr(), "updateItemId");
    EXPECT_STREQ(statuses[0].mSubjectID.CStr(), "updateSubjectId");
    EXPECT_STREQ(statuses[0].mManifestDigest.CStr(), "updateManifestDigest");
    EXPECT_STREQ(statuses[0].mVersion.CStr(), "1.0.1");

    for (const auto& entry : std::filesystem::directory_iterator(cWorkingDir)) {
        EXPECT_TRUE(std::find(cExpectedFiles.begin(), cExpectedFiles.end(), entry.path()) != cExpectedFiles.end())
            << "Unexpected file: " << entry.path();
    }

    err = mRootfsRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(RootfsRuntimeTest, Updated)
{
    const std::vector cExpectedFiles {cInstanceFile, cUpdateInstanceFile, cWorkingDir / "rootfs.1.0.1.squashfs",
        cWorkingDir / "updated", cWorkingDir / "do_apply"};

    if (std::ofstream file(cWorkingDir / "updated"); file.is_open()) {
    } else {
        FAIL() << "can't create updated file";
    }

    if (std::ofstream file(cWorkingDir / "rootfs.1.0.1.squashfs"); file.is_open()) {
        file << "1.0.1";
    } else {
        FAIL() << "can't create image file";
    }

    if (std::ofstream file(cVersionFile); file.is_open()) {
        file << R"(VERSION="1.0.1")";
    } else {
        throw std::runtime_error("can't create version file");
    }

    auto err = mRootfsRuntime.Init(
        mConfig, mCurrentNodeInfoProvider, mItemInfoProvider, mOCISpec, mStatusReceiver, mSystemdConn);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = mRootfsRuntime.Start();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    std::vector<InstanceStatus> statuses;

    err = mStatusReceiver.GetStatuses(statuses, std::chrono::seconds(1));
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eActivating) << statuses[0].mState.ToString().CStr();
    EXPECT_STREQ(statuses[0].mItemID.CStr(), "updateItemId");
    EXPECT_STREQ(statuses[0].mSubjectID.CStr(), "updateSubjectId");
    EXPECT_STREQ(statuses[0].mManifestDigest.CStr(), "updateManifestDigest");
    EXPECT_STREQ(statuses[0].mVersion.CStr(), "1.0.1");

    for (const auto& entry : std::filesystem::directory_iterator(cWorkingDir)) {
        EXPECT_TRUE(std::find(cExpectedFiles.begin(), cExpectedFiles.end(), entry.path()) != cExpectedFiles.end())
            << "Unexpected file: " << entry.path();
    }

    err = mRootfsRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(RootfsRuntimeTest, Failed)
{
    auto err = mRootfsRuntime.Init(
        mConfig, mCurrentNodeInfoProvider, mItemInfoProvider, mOCISpec, mStatusReceiver, mSystemdConn);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    if (std::ofstream file(cWorkingDir / "failed"); file.is_open()) {
    } else {
        FAIL() << "can't create failed file";
    }

    err = mRootfsRuntime.Start();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    std::vector<InstanceStatus> statuses;

    err = mStatusReceiver.GetStatuses(statuses, std::chrono::seconds(1));
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    ASSERT_EQ(statuses.size(), 2u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eFailed);
    EXPECT_STREQ(statuses[0].mItemID.CStr(), "updateItemId");
    EXPECT_STREQ(statuses[0].mSubjectID.CStr(), "updateSubjectId");
    EXPECT_STREQ(statuses[0].mManifestDigest.CStr(), "updateManifestDigest");
    EXPECT_STREQ(statuses[0].mVersion.CStr(), "1.0.1");

    EXPECT_EQ(statuses[1].mState, InstanceStateEnum::eActive);
    EXPECT_STREQ(statuses[1].mItemID.CStr(), "itemId");
    EXPECT_STREQ(statuses[1].mSubjectID.CStr(), "subjectId");
    EXPECT_STREQ(statuses[1].mManifestDigest.CStr(), "manifestDigest");
    EXPECT_STREQ(statuses[1].mVersion.CStr(), "1.0.0");

    for (const auto& entry : std::filesystem::directory_iterator(cWorkingDir)) {
        EXPECT_EQ(cInstanceFile, entry.path());
    }

    err = mRootfsRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

} // namespace aos::sm::launcher
