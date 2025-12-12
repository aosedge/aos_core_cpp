/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>

#include <Poco/JSON/Object.h>
#include <Poco/Process.h>

#include <gtest/gtest.h>

#include <core/common/tests/mocks/currentnodeinfoprovidermock.hpp>
#include <core/common/tests/mocks/ocispecmock.hpp>
#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>
#include <core/sm/tests/stubs/instancestatusreceiver.hpp>

#include <common/utils/exception.hpp>
#include <common/utils/time.hpp>

#include <sm/launcher/runtimes/rootfs/rootfs.hpp>
#include <sm/launcher/runtimes/utils/systemdrebooter.hpp>
#include <sm/tests/mocks/systemdconnmock.hpp>

using namespace testing;

namespace aos::sm::launcher::rootfs {

namespace {

/***********************************************************************************************************************
 * Consts
 **********************************************************************************************************************/

const auto cTestDir                  = std::filesystem::path("testRootfs");
const auto cUncompressedTestFile     = cTestDir / "testfile.txt";
const auto cMetadataDir              = cTestDir / "metadata";
const auto cUpdateDir                = cTestDir / "updateDir";
const auto cInstanceFile             = cMetadataDir / "instance.json";
const auto cUpdateInstanceFile       = cMetadataDir / "instance_update.json";
const auto cVersionFile              = cMetadataDir / "version.txt";
const auto cUpdateRootfsManifestFile = cTestDir / "manifest.json";
const auto cUpdateRootfsFile         = cTestDir / "rootfs.tar.gz";

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

/***********************************************************************************************************************
 * Mocks
 **********************************************************************************************************************/

class UpdateCheckerMock : public UpdateCheckerItf {
public:
    MOCK_METHOD(Error, Check, (), (override));
};

class RebooterMock : public RebooterItf {
public:
    MOCK_METHOD(Error, Reboot, (), (override));
};

class ImageManagerMock : public ImageManagerItf {
public:
    MOCK_METHOD(Error, GetBlobPath, (const String& digest, String& path), (const, override));
};

} // namespace

class RootfsTest : public Test {
protected:
    void SetUp() override
    {
        tests::utils::InitLog();

        std::filesystem::remove_all(cTestDir);

        std::filesystem::create_directories(cUpdateDir);
        std::filesystem::create_directories(cMetadataDir);

        mConfig.mUpdateDir   = cUpdateDir.c_str();
        mConfig.mRuntimeID   = "rootfsId";
        mConfig.mRuntimeType = "rootfs";

        mConfig.mCurrentInstanceFile = cInstanceFile.c_str();
        mConfig.mCurrentVersionFile  = cVersionFile.c_str();

        mConfig.mUpdateInstanceFile = cUpdateInstanceFile.c_str();

        WriteFiles();

        EXPECT_CALL(mCurrentNodeInfoProvider, GetCurrentNodeInfo(_)).WillRepeatedly(Invoke([](NodeInfo& nodeInfo) {
            nodeInfo.mNodeID = "nodeId";

            return ErrorEnum::eNone;
        }));
    }

    void WriteFiles()
    {
        if (std::ofstream file(cInstanceFile); file.is_open()) {
            file << R"({
                "itemId": "itemId",
                "subjectId": "subjectId",
                "manifestDigest": "manifestDigest"
            })";
        } else {
            throw std::runtime_error("can't create instance file");
        }

        if (std::ofstream file(cVersionFile); file.is_open()) {
            file << "1.0.0";
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
                "manifestDigest": "updateManifestDigest"
            })";
        } else {
            throw std::runtime_error("can't create manifest file");
        }
    }

    RootfsConfig                           mConfig;
    iamclient::CurrentNodeInfoProviderMock mCurrentNodeInfoProvider;
    ImageManagerMock                       mImageManager;
    oci::OCISpecMock                       mOCISpec;
    InstanceStatusReceiverStub             mStatusReceiver;
    UpdateCheckerMock                      mUpdateChecker;
    RebooterMock                           mRebooter;
    RootfsRuntime                          mRootfsRuntime;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(RootfsTest, GetRuntimeInfo)
{
    const auto cExpectedRuntimeID = "rootfs:nodeId";

    auto err = mRootfsRuntime.Init(
        mConfig, mCurrentNodeInfoProvider, mImageManager, mOCISpec, mStatusReceiver, mUpdateChecker, mRebooter);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = mRootfsRuntime.Start();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    auto info = std::make_unique<RuntimeInfo>();

    err = mRootfsRuntime.GetRuntimeInfo(*info);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    mConfig.mRuntimeID = cExpectedRuntimeID;

    EXPECT_EQ(*info, static_cast<const RuntimeInfo&>(mConfig));
}

TEST_F(RootfsTest, StartInstance)
{
    const String cUpdateLayerDigest = "updateRootfsDigest";
    const String cBlobPath          = "rootfsImageDigest";

    CreateGZIP(cUpdateRootfsFile.c_str());

    auto err = mRootfsRuntime.Init(
        mConfig, mCurrentNodeInfoProvider, mImageManager, mOCISpec, mStatusReceiver, mUpdateChecker, mRebooter);
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

    EXPECT_CALL(mImageManager, GetBlobPath(cBlobPath, _))
        .WillOnce(DoAll(SetArgReferee<1>(String(cUpdateRootfsManifestFile.c_str())), Return(ErrorEnum::eNone)));

    EXPECT_CALL(mImageManager, GetBlobPath(cUpdateLayerDigest, _))
        .WillOnce(DoAll(SetArgReferee<1>(String(cUpdateRootfsFile.c_str())), Return(ErrorEnum::eNone)));

    err = mRootfsRuntime.StartInstance(*instanceInfo, *status);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(status->mState, InstanceStateEnum::eActivating) << status->mState.ToString().CStr();

    EXPECT_TRUE(std::filesystem::exists(cUpdateDir / "do_update"));

    std::vector<StaticString<cIDLen>> rebootRuntimes;

    err = mStatusReceiver.GetRuntimesToReboot(rebootRuntimes, std::chrono::seconds(1));
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    ASSERT_EQ(rebootRuntimes.size(), 1u);
    EXPECT_STREQ(rebootRuntimes[0].CStr(), "rootfs:nodeId");
}

TEST_F(RootfsTest, StartInstanceLoadImageManifestFailed)
{
    auto err = mRootfsRuntime.Init(
        mConfig, mCurrentNodeInfoProvider, mImageManager, mOCISpec, mStatusReceiver, mUpdateChecker, mRebooter);
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

    EXPECT_TRUE(std::filesystem::is_empty(cUpdateDir));
}

TEST_F(RootfsTest, NoPendingUpdates)
{
    auto err = mRootfsRuntime.Init(
        mConfig, mCurrentNodeInfoProvider, mImageManager, mOCISpec, mStatusReceiver, mUpdateChecker, mRebooter);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = mRootfsRuntime.Start();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(RootfsTest, DoUpdateSucceded)
{
    auto err = mRootfsRuntime.Init(
        mConfig, mCurrentNodeInfoProvider, mImageManager, mOCISpec, mStatusReceiver, mUpdateChecker, mRebooter);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    if (std::ofstream file(cUpdateDir / "do_update"); file.is_open()) {
    } else {
        FAIL() << "can't create do_update file";
    }

    EXPECT_CALL(mUpdateChecker, Check()).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(mRebooter, Reboot()).WillOnce(Return(ErrorEnum::eNone));

    err = mRootfsRuntime.Start();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    std::vector<InstanceStatus> statuses;

    err = mStatusReceiver.GetStatuses(statuses, std::chrono::seconds(1));
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eActivating);
    EXPECT_STREQ(statuses[0].mItemID.CStr(), "itemId");
    EXPECT_STREQ(statuses[0].mSubjectID.CStr(), "subjectId");
    EXPECT_STREQ(statuses[0].mManifestDigest.CStr(), "manifestDigest");
    EXPECT_STREQ(statuses[0].mVersion.CStr(), "1.0.0");

    EXPECT_TRUE(std::filesystem::exists(cUpdateDir / "do_apply"));
    EXPECT_FALSE(std::filesystem::exists(cUpdateDir / "do_update"));
}

TEST_F(RootfsTest, DoUpdateFailed)
{
    auto err = mRootfsRuntime.Init(
        mConfig, mCurrentNodeInfoProvider, mImageManager, mOCISpec, mStatusReceiver, mUpdateChecker, mRebooter);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    if (std::ofstream file(cUpdateDir / "do_update"); file.is_open()) {
    } else {
        FAIL() << "can't create do_update file";
    }

    EXPECT_CALL(mUpdateChecker, Check()).WillOnce(Return(ErrorEnum::eFailed));
    EXPECT_CALL(mRebooter, Reboot()).WillOnce(Return(ErrorEnum::eNone));

    err = mRootfsRuntime.Start();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    std::vector<InstanceStatus> statuses;

    err = mStatusReceiver.GetStatuses(statuses, std::chrono::seconds(1));
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eFailed);
    EXPECT_STREQ(statuses[0].mItemID.CStr(), "itemId");
    EXPECT_STREQ(statuses[0].mSubjectID.CStr(), "subjectId");
    EXPECT_STREQ(statuses[0].mManifestDigest.CStr(), "manifestDigest");
    EXPECT_STREQ(statuses[0].mVersion.CStr(), "1.0.0");

    EXPECT_TRUE(std::filesystem::exists(cUpdateDir / "failed"));
    EXPECT_FALSE(std::filesystem::exists(cUpdateDir / "do_update"));
    EXPECT_FALSE(std::filesystem::exists(cUpdateDir / "do_apply"));
}

TEST_F(RootfsTest, Updated)
{
    auto err = mRootfsRuntime.Init(
        mConfig, mCurrentNodeInfoProvider, mImageManager, mOCISpec, mStatusReceiver, mUpdateChecker, mRebooter);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    if (std::ofstream file(cUpdateDir / "updated"); file.is_open()) {
    } else {
        FAIL() << "can't create updated file";
    }

    EXPECT_CALL(mUpdateChecker, Check()).Times(0);
    EXPECT_CALL(mRebooter, Reboot()).Times(0);

    err = mRootfsRuntime.Start();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    std::vector<InstanceStatus> statuses;

    err = mStatusReceiver.GetStatuses(statuses, std::chrono::seconds(1));
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eActive) << statuses[0].mState.ToString().CStr();
    EXPECT_STREQ(statuses[0].mItemID.CStr(), "updateItemId");
    EXPECT_STREQ(statuses[0].mSubjectID.CStr(), "updateSubjectId");
    EXPECT_STREQ(statuses[0].mManifestDigest.CStr(), "updateManifestDigest");
    EXPECT_STREQ(statuses[0].mVersion.CStr(), "1.0.0");

    LOG_DBG() << Log::Field(statuses[0].mError);

    EXPECT_TRUE(std::filesystem::is_empty(cUpdateDir));
}

TEST_F(RootfsTest, Failed)
{
    auto err = mRootfsRuntime.Init(
        mConfig, mCurrentNodeInfoProvider, mImageManager, mOCISpec, mStatusReceiver, mUpdateChecker, mRebooter);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    if (std::ofstream file(cUpdateDir / "failed"); file.is_open()) {
    } else {
        FAIL() << "can't create failed file";
    }

    EXPECT_CALL(mUpdateChecker, Check()).Times(0);
    EXPECT_CALL(mRebooter, Reboot()).Times(0);

    err = mRootfsRuntime.Start();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    std::vector<InstanceStatus> statuses;

    err = mStatusReceiver.GetStatuses(statuses, std::chrono::seconds(1));
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eFailed);
    EXPECT_STREQ(statuses[0].mItemID.CStr(), "itemId");
    EXPECT_STREQ(statuses[0].mSubjectID.CStr(), "subjectId");
    EXPECT_STREQ(statuses[0].mManifestDigest.CStr(), "manifestDigest");
    EXPECT_STREQ(statuses[0].mVersion.CStr(), "1.0.0");

    EXPECT_TRUE(std::filesystem::is_empty(cUpdateDir));
}

} // namespace aos::sm::launcher::rootfs
