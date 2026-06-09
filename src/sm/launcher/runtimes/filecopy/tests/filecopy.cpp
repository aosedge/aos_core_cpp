/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>

#include <Poco/JSON/Object.h>
#include <Poco/Process.h>
#include <Poco/UUIDGenerator.h>

#include <gtest/gtest.h>

#include <core/common/tests/mocks/currentnodeinfoprovidermock.hpp>
#include <core/common/tests/mocks/ocispecmock.hpp>
#include <core/common/tests/utils/log.hpp>
#include <core/common/tests/utils/utils.hpp>
#include <core/sm/tests/mocks/iteminfoprovidermock.hpp>
#include <core/sm/tests/stubs/instancestatusreceiver.hpp>

#include <core/common/ocispec/itf/imagespec.hpp>

#include <sm/launcher/runtimes/filecopy/filecopy.hpp>
#include <sm/tests/mocks/systemdconnmock.hpp>

using namespace testing;

namespace aos {

std::ostream& operator<<(std::ostream& os, const String& str)
{
    return os << str.CStr();
}

std::ostream& operator<<(std::ostream& os, const InstanceStatus& info)
{
    return os << info.mItemID << ":" << info.mSubjectID << ":" << info.mInstance << ":" << info.mNodeID << ":"
              << info.mRuntimeID << ":" << info.mManifestDigest << ":" << info.mVersion;
}

} // namespace aos

namespace aos::sm::launcher {

namespace {

const auto cTestDir        = std::filesystem::path("testFileCopy");
const auto cInstallDir     = cTestDir / "install";
const auto cMetadataDir    = cTestDir / "metadata";
const auto cComponentImage = cInstallDir / "image.squashfs";
const auto cInstanceFile   = cMetadataDir / "installed_instance.json";
const auto cSourceImage    = cTestDir / "source.squashfs";
const auto cSourceTarGzip  = cTestDir / "source.tar.gz";
const auto cTarContentFile = cTestDir / "content.txt";

void CreateTarGzip(const std::filesystem::path& archive, const std::filesystem::path& contentFile)
{
    if (std::ofstream f(contentFile); f.is_open()) {
        f << "component content";
    } else {
        throw std::runtime_error("can't create content file for archive");
    }

    Poco::Process::Args args
        = {"-czf", archive.string(), "-C", contentFile.parent_path().string(), contentFile.filename().string()};

    if (int rc = Poco::Process::launch("tar", args).wait(); rc != 0) {
        throw std::runtime_error("failed to create tar archive");
    }
}

} // namespace

class FileCopyRuntimeTest : public Test {
protected:
    static void SetUpTestSuite() { tests::utils::InitLog(); }

    void SetUp() override
    {
        std::filesystem::remove_all(cTestDir);
        std::filesystem::create_directories(cInstallDir);
        std::filesystem::create_directories(cMetadataDir);

        mConfig.isComponent = true;
        mConfig.mPlugin     = "filecopy";
        mConfig.mType       = "mycomponent";

        {
            auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

            json->set("targetPath", cInstallDir.string());
            json->set("runtimeDir", cMetadataDir.string());

            mConfig.mConfig = json;
        }

        EXPECT_CALL(mCurrentNodeInfoProvider, GetCurrentNodeInfo(_)).WillRepeatedly(Invoke([](NodeInfo& nodeInfo) {
            nodeInfo.mNodeID   = "nodeId";
            nodeInfo.mNodeType = "nodeType";

            nodeInfo.mCPUs.EmplaceBack();
            nodeInfo.mCPUs[0].mArchInfo.mArchitecture = "amd64";

            nodeInfo.mOSInfo.mOS = "linux";

            return ErrorEnum::eNone;
        }));

        if (std::ofstream f(cSourceImage); f.is_open()) {
            f << "fake squashfs content";
        } else {
            throw std::runtime_error("can't create source image file");
        }
    }

    std::string GetExpectedRuntimeID() const
    {
        return Poco::UUIDGenerator::defaultGenerator()
            .createFromName(Poco::UUID::oid(), "mycomponent-nodeId")
            .toString();
    }

    void WriteInstalledInstance(const std::string& itemId = "itemId", const std::string& subjectId = "subjectId",
        const std::string& version = "1.0.0", const std::string& digest = "manifestDigest", bool preinstalled = false)
    {
        if (std::ofstream file(cInstanceFile); file.is_open()) {
            file << R"({"itemId": ")" << itemId << R"(", "subjectId": ")" << subjectId
                 << R"(", "instance": 0, "manifestDigest": ")" << digest << R"(", "version": ")" << version
                 << R"(", "preinstalled": )" << (preinstalled ? "true" : "false") << "}";
        } else {
            throw std::runtime_error("can't create instance file");
        }
    }

    RuntimeConfig                          mConfig;
    iamclient::CurrentNodeInfoProviderMock mCurrentNodeInfoProvider;
    imagemanager::ItemInfoProviderMock     mItemInfoProvider;
    oci::OCISpecMock                       mOCISpec;
    InstanceStatusReceiverStub             mStatusReceiver;
    sm::utils::SystemdConnMock             mSystemdConn;
    FileCopyRuntime                        mRuntime;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(FileCopyRuntimeTest, GetRuntimeInfo)
{
    auto err
        = mRuntime.Init(mConfig, mCurrentNodeInfoProvider, mItemInfoProvider, mOCISpec, mStatusReceiver, mSystemdConn);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = mRuntime.Start();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    auto info = std::make_unique<RuntimeInfo>();

    err = mRuntime.GetRuntimeInfo(*info);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_STREQ(info->mRuntimeType.CStr(), "mycomponent");
    EXPECT_EQ(info->mMaxInstances, 1u);
    EXPECT_STREQ(info->mRuntimeID.CStr(), GetExpectedRuntimeID().c_str());
    EXPECT_STREQ(info->mArchInfo.mArchitecture.CStr(), "amd64");
    EXPECT_STREQ(info->mOSInfo.mOS.CStr(), "linux");

    err = mRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(FileCopyRuntimeTest, StartFreshPreinstalled)
{
    auto err
        = mRuntime.Init(mConfig, mCurrentNodeInfoProvider, mItemInfoProvider, mOCISpec, mStatusReceiver, mSystemdConn);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = mRuntime.Start();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    std::vector<InstanceStatus> statuses;

    err = mStatusReceiver.GetStatuses(statuses, std::chrono::seconds(1));
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eActive);
    EXPECT_STREQ(statuses[0].mItemID.CStr(), "mycomponent");
    EXPECT_STREQ(statuses[0].mSubjectID.CStr(), "nodeType");
    EXPECT_TRUE(statuses[0].mPreinstalled);
    EXPECT_EQ(statuses[0].mInstance, 0u);
    EXPECT_STREQ(statuses[0].mVersion.CStr(), "0.0.0");

    EXPECT_TRUE(std::filesystem::exists(cInstanceFile));

    err = mRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(FileCopyRuntimeTest, StartWithInstalledInstance)
{
    WriteInstalledInstance("itemId", "subjectId", "1.0.0", "digest1");

    auto err
        = mRuntime.Init(mConfig, mCurrentNodeInfoProvider, mItemInfoProvider, mOCISpec, mStatusReceiver, mSystemdConn);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = mRuntime.Start();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    std::vector<InstanceStatus> statuses;

    err = mStatusReceiver.GetStatuses(statuses, std::chrono::seconds(1));
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eActive);
    EXPECT_STREQ(statuses[0].mItemID.CStr(), "itemId");
    EXPECT_STREQ(statuses[0].mSubjectID.CStr(), "subjectId");
    EXPECT_STREQ(statuses[0].mVersion.CStr(), "1.0.0");
    EXPECT_STREQ(statuses[0].mManifestDigest.CStr(), "digest1");

    err = mRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(FileCopyRuntimeTest, StartInstance)
{
    const String cManifestDigest = "manifestDigest";
    const String cLayerDigest    = "layerDigest";

    auto err
        = mRuntime.Init(mConfig, mCurrentNodeInfoProvider, mItemInfoProvider, mOCISpec, mStatusReceiver, mSystemdConn);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = mRuntime.Start();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_CALL(mOCISpec, LoadImageManifest(_, _))
        .WillOnce(Invoke([cLayerDigest](const String&, oci::ImageManifest& manifest) {
            manifest.mLayers.Resize(1);
            manifest.mLayers[0].mDigest = cLayerDigest;

            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(mItemInfoProvider, GetBlobPath(cManifestDigest, _))
        .WillOnce(DoAll(SetArgReferee<1>(String("manifestBlobPath")), Return(ErrorEnum::eNone)));

    EXPECT_CALL(mItemInfoProvider, GetBlobPath(cLayerDigest, _))
        .WillOnce(DoAll(SetArgReferee<1>(String(cSourceImage.c_str())), Return(ErrorEnum::eNone)));

    auto instanceInfo             = std::make_unique<InstanceInfo>();
    instanceInfo->mManifestDigest = cManifestDigest;
    instanceInfo->mItemID         = "itemId";
    instanceInfo->mSubjectID      = "subjectId";
    instanceInfo->mVersion        = "1.0.0";

    auto status = std::make_unique<InstanceStatus>();

    err = mRuntime.StartInstance(*instanceInfo, *status);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(status->mState, InstanceStateEnum::eActivating);
    EXPECT_TRUE(std::filesystem::exists(cComponentImage));
    EXPECT_TRUE(std::filesystem::exists(cInstanceFile));

    std::vector<StaticString<cIDLen>> rebootRuntimes;

    err = mStatusReceiver.GetRuntimesToReboot(rebootRuntimes, std::chrono::seconds(1));
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    ASSERT_EQ(rebootRuntimes.size(), 1u);
    EXPECT_STREQ(rebootRuntimes[0].CStr(), GetExpectedRuntimeID().c_str());

    err = mRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(FileCopyRuntimeTest, StartInstanceTarGzip)
{
    const String cManifestDigest = "manifestDigest";
    const String cLayerDigest    = "layerDigest";

    CreateTarGzip(cSourceTarGzip, cTarContentFile);

    auto err
        = mRuntime.Init(mConfig, mCurrentNodeInfoProvider, mItemInfoProvider, mOCISpec, mStatusReceiver, mSystemdConn);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = mRuntime.Start();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_CALL(mOCISpec, LoadImageManifest(_, _))
        .WillOnce(Invoke([cLayerDigest](const String&, oci::ImageManifest& manifest) {
            manifest.mLayers.Resize(1);
            manifest.mLayers[0].mDigest    = cLayerDigest;
            manifest.mLayers[0].mMediaType = oci::cMediaTypeComponentFullTarGZip;

            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(mItemInfoProvider, GetBlobPath(cManifestDigest, _))
        .WillOnce(DoAll(SetArgReferee<1>(String("manifestBlobPath")), Return(ErrorEnum::eNone)));

    EXPECT_CALL(mItemInfoProvider, GetBlobPath(cLayerDigest, _))
        .WillOnce(DoAll(SetArgReferee<1>(String(cSourceTarGzip.c_str())), Return(ErrorEnum::eNone)));

    auto instanceInfo             = std::make_unique<InstanceInfo>();
    instanceInfo->mManifestDigest = cManifestDigest;
    instanceInfo->mItemID         = "itemId";
    instanceInfo->mSubjectID      = "subjectId";
    instanceInfo->mVersion        = "1.0.0";

    auto status = std::make_unique<InstanceStatus>();

    err = mRuntime.StartInstance(*instanceInfo, *status);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(status->mState, InstanceStateEnum::eActivating);
    EXPECT_TRUE(std::filesystem::exists(cInstallDir / cTarContentFile.filename()));
    EXPECT_FALSE(std::filesystem::exists(cComponentImage));

    err = mRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(FileCopyRuntimeTest, StartInstanceSameDigest)
{
    WriteInstalledInstance("itemId", "subjectId", "1.0.0", "digest1");

    auto err
        = mRuntime.Init(mConfig, mCurrentNodeInfoProvider, mItemInfoProvider, mOCISpec, mStatusReceiver, mSystemdConn);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = mRuntime.Start();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    std::vector<InstanceStatus> startStatuses;
    err = mStatusReceiver.GetStatuses(startStatuses, std::chrono::seconds(1));
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    auto instanceInfo             = std::make_unique<InstanceInfo>();
    instanceInfo->mItemID         = "itemId";
    instanceInfo->mSubjectID      = "subjectId";
    instanceInfo->mManifestDigest = "digest1";
    instanceInfo->mVersion        = "1.0.0";

    auto status = std::make_unique<InstanceStatus>();

    err = mRuntime.StartInstance(*instanceInfo, *status);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(status->mState, InstanceStateEnum::eActive);

    err = mRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(FileCopyRuntimeTest, StartInstanceLoadManifestFailed)
{
    auto err
        = mRuntime.Init(mConfig, mCurrentNodeInfoProvider, mItemInfoProvider, mOCISpec, mStatusReceiver, mSystemdConn);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = mRuntime.Start();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_CALL(mItemInfoProvider, GetBlobPath(_, _))
        .WillOnce(DoAll(SetArgReferee<1>(String("manifestBlobPath")), Return(ErrorEnum::eNone)));

    EXPECT_CALL(mOCISpec, LoadImageManifest(_, _)).WillOnce(Return(ErrorEnum::eInvalidChecksum));

    auto instanceInfo             = std::make_unique<InstanceInfo>();
    instanceInfo->mManifestDigest = "newDigest";
    instanceInfo->mItemID         = "itemId";
    instanceInfo->mSubjectID      = "subjectId";

    auto status = std::make_unique<InstanceStatus>();

    err = mRuntime.StartInstance(*instanceInfo, *status);
    ASSERT_TRUE(err.Is(ErrorEnum::eInvalidChecksum)) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(status->mState, InstanceStateEnum::eFailed);
    EXPECT_FALSE(std::filesystem::exists(cComponentImage));

    err = mRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

} // namespace aos::sm::launcher
