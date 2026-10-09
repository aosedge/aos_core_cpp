/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <atomic>
#include <filesystem>
#include <fstream>
#include <future>
#include <set>
#include <sstream>

#include <Poco/DigestEngine.h>
#include <Poco/DigestStream.h>
#include <Poco/JSON/Object.h>
#include <Poco/Process.h>
#include <Poco/SHA2Engine.h>
#include <Poco/StreamCopier.h>
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

std::string CalculateSHA256(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("can't open file");
    }

    Poco::SHA2Engine         engine;
    Poco::DigestOutputStream dos(engine);

    Poco::StreamCopier::copyStream(file, dos);
    dos.close();

    return "sha256:" + Poco::DigestEngine::digestToHex(engine.digest());
}

std::string GetBootID()
{
    std::ifstream file("/proc/sys/kernel/random/boot_id");
    std::string   bootID;

    std::getline(file, bootID);

    return bootID;
}

std::string ReadFile(const std::filesystem::path& path)
{
    std::ifstream     file(path);
    std::stringstream buffer;

    buffer << file.rdbuf();

    return buffer.str();
}

void WriteFile(const std::filesystem::path& path, const std::string& content = "")
{
    if (std::ofstream file(path); file.is_open()) {
        file << content;
    } else {
        throw std::runtime_error("can't create file");
    }
}

std::set<std::filesystem::path> GetWorkingDirFiles()
{
    std::set<std::filesystem::path> files;

    for (const auto& entry : std::filesystem::directory_iterator(cWorkingDir)) {
        files.insert(entry.path());
    }

    return files;
}

class InstanceStatusReceiverFailingStub : public InstanceStatusReceiverStub {
public:
    Error RebootRequired(const String& runtimeID) override
    {
        if (mFailReboot) {
            return ErrorEnum::eFailed;
        }

        return InstanceStatusReceiverStub::RebootRequired(runtimeID);
    }

    std::atomic_bool mFailReboot {};
};

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

            nodeInfo.mCPUs.EmplaceBack();
            nodeInfo.mCPUs[0].mArchInfo.mArchitecture = "amd64";

            nodeInfo.mOSInfo.mOS = "linux";

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
                "version": "1.0.0",
                "preinstalled": true
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

    void WritePendingInstance(const std::string& bootID = "", const std::string& version = "1.0.1",
        bool confirmed = false, const std::string& manifestDigest = "updateManifestDigest")
    {
        auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

        json->set("itemId", "updateItemId");
        json->set("subjectId", "updateSubjectId");
        json->set("manifestDigest", manifestDigest);
        json->set("version", version);
        json->set("confirmed", confirmed);

        if (!bootID.empty()) {
            json->set("bootId", bootID);
        }

        std::ostringstream content;

        json->stringify(content);

        WriteFile(cUpdateInstanceFile, content.str());
    }

    void ExpectUpdateImage(const std::string& manifestDigest, const std::string& layerDigest,
        const std::string& mediaType = "vnd.aos.image.component.full.v1+gzip", size_t size = 0)
    {
        EXPECT_CALL(mItemInfoProvider, GetBlobPath(StrEq(manifestDigest), _))
            .WillOnce(Invoke(SetPath(cUpdateRootfsManifestFile)));

        EXPECT_CALL(mOCISpec, LoadImageManifest(StrEq(cUpdateRootfsManifestFile.string()), _))
            .WillOnce(Invoke([layerDigest, mediaType, size](const String&, oci::ImageManifest& manifest) {
                manifest.mLayers.Resize(1);
                manifest.mLayers[0].mDigest    = layerDigest.c_str();
                manifest.mLayers[0].mMediaType = mediaType.c_str();
                manifest.mLayers[0].mSize      = size;

                return ErrorEnum::eNone;
            }));
    }

    void ExpectLayerBlob(const std::string& layerDigest, const std::filesystem::path& path = cUpdateRootfsFile)
    {
        EXPECT_CALL(mItemInfoProvider, GetBlobPath(StrEq(layerDigest), _)).WillOnce(Invoke(SetPath(path)));
    }

    static Matcher<const String&> StrEq(const std::string& expected)
    {
        return Truly([expected](const String& value) { return expected == value.CStr(); });
    }

    static std::function<Error(const String&, String&)> SetPath(const std::filesystem::path& path)
    {
        return [path](const String&, String& result) { return result.Assign(path.c_str()); };
    }

    std::unique_ptr<InstanceInfo> CreateInstance(const std::string& itemID, const std::string& manifestDigest,
        const std::string& version = "1.0.1", const std::string& subjectID = "subjectId") const
    {
        auto instance             = std::make_unique<InstanceInfo>();
        instance->mItemID         = itemID.c_str();
        instance->mSubjectID      = subjectID.c_str();
        instance->mManifestDigest = manifestDigest.c_str();
        instance->mVersion        = version.c_str();
        instance->mType           = UpdateItemTypeEnum::eComponent;

        return instance;
    }

    void InitAndStart()
    {
        auto err = mRootfsRuntime.Init(
            mConfig, mCurrentNodeInfoProvider, mItemInfoProvider, mOCISpec, mStatusReceiver, mSystemdConn);
        ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

        err = mRootfsRuntime.Start();
        ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
    }

    std::vector<InstanceStatus> GetStatuses()
    {
        std::vector<InstanceStatus> statuses;

        auto err = mStatusReceiver.GetStatuses(statuses, std::chrono::seconds(1));
        EXPECT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

        return statuses;
    }

    bool IsRebootRequested()
    {
        std::vector<StaticString<cIDLen>> runtimes;

        return mStatusReceiver.GetRuntimesToReboot(runtimes, std::chrono::milliseconds(100)).IsNone();
    }

    RuntimeConfig                          mConfig;
    iamclient::CurrentNodeInfoProviderMock mCurrentNodeInfoProvider;
    imagemanager::ItemInfoProviderMock     mItemInfoProvider;
    oci::OCISpecMock                       mOCISpec;
    InstanceStatusReceiverFailingStub      mStatusReceiver;
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

    EXPECT_STREQ(info->mArchInfo.mArchitecture.CStr(), "amd64");
    EXPECT_STREQ(info->mOSInfo.mOS.CStr(), "linux");
    err = mRootfsRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(RootfsRuntimeTest, StartInstance)
{
    CreateGZIP(cUpdateRootfsFile.c_str());

    const auto cLayerDigest = CalculateSHA256(cUpdateRootfsFile);

    InitAndStart();

    auto instanceInfo = CreateInstance("itemId", "rootfsImageDigest");
    auto status       = std::make_unique<InstanceStatus>();

    ExpectUpdateImage("rootfsImageDigest", cLayerDigest, "vnd.aos.image.component.full.v1+gzip",
        std::filesystem::file_size(cUpdateRootfsFile));
    ExpectLayerBlob(cLayerDigest);

    auto err = mRootfsRuntime.StartInstance(*instanceInfo, *status);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(status->mState, InstanceStateEnum::eActivating) << status->mState.ToString().CStr();

    EXPECT_EQ(GetWorkingDirFiles(),
        (std::set {cInstanceFile, cUpdateInstanceFile, cWorkingDir / "image.squashfs", cWorkingDir / "do_update"}));
    EXPECT_EQ(ReadFile(cWorkingDir / "do_update"), "full");
    EXPECT_EQ(CalculateSHA256(cWorkingDir / "image.squashfs"), cLayerDigest);
    EXPECT_NE(ReadFile(cUpdateInstanceFile).find(GetBootID()), std::string::npos);
    EXPECT_NE(ReadFile(cUpdateInstanceFile).find(R"("confirmed":false)"), std::string::npos);

    std::vector<StaticString<cIDLen>> rebootRuntimes;

    err = mStatusReceiver.GetRuntimesToReboot(rebootRuntimes, std::chrono::seconds(1));
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    ASSERT_EQ(rebootRuntimes.size(), 1u);
    EXPECT_STREQ(rebootRuntimes[0].CStr(), GetExpectedRuntimeID().c_str());

    // Start the same instance again: update is pending, no new update should be prepared.

    err = mRootfsRuntime.StartInstance(*instanceInfo, *status);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(status->mState, InstanceStateEnum::eActivating) << status->mState.ToString().CStr();

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

    WritePendingInstance("previousBootId", "1.0.1", true);

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

    ASSERT_EQ(statuses.size(), 2u);

    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eInactive);
    EXPECT_STREQ(statuses[0].mItemID.CStr(), "itemId");
    EXPECT_STREQ(statuses[0].mSubjectID.CStr(), "subjectId");
    EXPECT_STREQ(statuses[0].mManifestDigest.CStr(), "manifestDigest");
    EXPECT_STREQ(statuses[0].mVersion.CStr(), "1.0.0");

    EXPECT_EQ(statuses[1].mState, InstanceStateEnum::eActive);
    EXPECT_STREQ(statuses[1].mItemID.CStr(), "updateItemId");
    EXPECT_STREQ(statuses[1].mSubjectID.CStr(), "updateSubjectId");
    EXPECT_STREQ(statuses[1].mManifestDigest.CStr(), "updateManifestDigest");
    EXPECT_STREQ(statuses[1].mVersion.CStr(), "1.0.1");

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

    // Stop joins health check thread: verdict is stored after it.
    err = mRootfsRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    for (const auto& entry : std::filesystem::directory_iterator(cWorkingDir)) {
        EXPECT_TRUE(std::find(cExpectedFiles.begin(), cExpectedFiles.end(), entry.path()) != cExpectedFiles.end())
            << "Unexpected file: " << entry.path();
    }
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

TEST_F(RootfsRuntimeTest, FailedReasonIsReported)
{
    WriteFile(cWorkingDir / "failed", "update not confirmed\n");
    WriteFile(cWorkingDir / "image.squashfs", "image");

    InitAndStart();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 2u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eFailed);
    EXPECT_STREQ(statuses[0].mItemID.CStr(), "updateItemId");
    EXPECT_STREQ(statuses[0].mError.Message(), "update not confirmed");
    EXPECT_EQ(statuses[1].mState, InstanceStateEnum::eActive);
    EXPECT_STREQ(statuses[1].mItemID.CStr(), "itemId");

    EXPECT_EQ(GetWorkingDirFiles(), (std::set {cInstanceFile}));

    // Failed update should not be retried on start instance request.

    auto status = std::make_unique<InstanceStatus>();

    auto err = mRootfsRuntime.StartInstance(
        *CreateInstance("updateItemId", "updateManifestDigest", "1.0.1", "updateSubjectId"), *status);
    EXPECT_TRUE(err.Is(ErrorEnum::eFailed)) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(status->mState, InstanceStateEnum::eFailed);
    EXPECT_STREQ(status->mError.Message(), "update not confirmed");

    err = mRootfsRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(RootfsRuntimeTest, CorruptedInstalledInstanceIsRestored)
{
    WriteFile(cInstanceFile, "");
    std::filesystem::remove(cUpdateInstanceFile);

    InitAndStart();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eActive);
    EXPECT_STREQ(statuses[0].mItemID.CStr(), "rootfs");
    EXPECT_STREQ(statuses[0].mSubjectID.CStr(), "nodeType");
    EXPECT_STREQ(statuses[0].mVersion.CStr(), "1.0.0");
    EXPECT_TRUE(statuses[0].mPreinstalled);

    EXPECT_EQ(GetWorkingDirFiles(), (std::set {cInstanceFile}));
    EXPECT_NE(ReadFile(cInstanceFile).find(R"("version":"1.0.0")"), std::string::npos);

    auto err = mRootfsRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(RootfsRuntimeTest, IncompleteInstalledInstanceIsRestored)
{
    WriteFile(cInstanceFile, R"({"itemId": "", "version": ""})");
    std::filesystem::remove(cUpdateInstanceFile);

    InitAndStart();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_STREQ(statuses[0].mItemID.CStr(), "rootfs");
    EXPECT_STREQ(statuses[0].mVersion.CStr(), "1.0.0");

    auto err = mRootfsRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(RootfsRuntimeTest, StartFailsWithoutInstalledInstanceAndVersionFile)
{
    std::filesystem::remove(cInstanceFile);
    std::filesystem::remove(cVersionFile);

    auto err = mRootfsRuntime.Init(
        mConfig, mCurrentNodeInfoProvider, mItemInfoProvider, mOCISpec, mStatusReceiver, mSystemdConn);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = mRootfsRuntime.Start();
    EXPECT_TRUE(err.Is(ErrorEnum::eNotFound)) << tests::utils::ErrorToStr(err);

    WriteFile(cVersionFile, "invalid");

    err = mRootfsRuntime.Start();
    EXPECT_TRUE(err.Is(ErrorEnum::eInvalidArgument)) << tests::utils::ErrorToStr(err);
}

TEST_F(RootfsRuntimeTest, CorruptedPendingInstanceCancelsUpdate)
{
    WriteFile(cUpdateInstanceFile, R"({"itemId": "updateItemId", "subj)");
    WriteFile(cWorkingDir / "do_update", "full");
    WriteFile(cWorkingDir / "image.squashfs", "image");
    WriteFile(cWorkingDir / "image.squashfs.tmp", "image");

    InitAndStart();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eActive);
    EXPECT_STREQ(statuses[0].mItemID.CStr(), "itemId");

    EXPECT_EQ(GetWorkingDirFiles(), (std::set {cInstanceFile}));
    EXPECT_FALSE(IsRebootRequested());

    auto err = mRootfsRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(RootfsRuntimeTest, CorruptedPendingInstanceRevertsTrialBoot)
{
    WriteFile(cUpdateInstanceFile, "");
    WriteFile(cWorkingDir / "do_update", "full");
    WriteFile(cWorkingDir / "updated");
    WriteFile(cWorkingDir / "do_apply");
    WriteFile(cWorkingDir / "image.squashfs", "image");

    InitAndStart();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eActive);
    EXPECT_STREQ(statuses[0].mItemID.CStr(), "itemId");

    EXPECT_EQ(GetWorkingDirFiles(),
        (std::set {cInstanceFile, cWorkingDir / "do_update", cWorkingDir / "updated", cWorkingDir / "failed",
            cWorkingDir / "image.squashfs"}));
    EXPECT_TRUE(IsRebootRequested());

    auto err = mRootfsRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(RootfsRuntimeTest, PendingWithoutActionIsFailed)
{
    // Power off during update preparation: pending instance is stored but do_update is not.

    WritePendingInstance(GetBootID());
    WriteFile(cWorkingDir / "image.squashfs", "image");

    InitAndStart();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 2u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eFailed);
    EXPECT_STREQ(statuses[0].mItemID.CStr(), "updateItemId");
    EXPECT_STREQ(statuses[0].mError.Message(), "rootfs version mismatch");
    EXPECT_EQ(statuses[1].mState, InstanceStateEnum::eActive);
    EXPECT_STREQ(statuses[1].mItemID.CStr(), "itemId");
    EXPECT_STREQ(statuses[1].mVersion.CStr(), "1.0.0");

    EXPECT_EQ(GetWorkingDirFiles(), (std::set {cInstanceFile}));
    EXPECT_NE(ReadFile(cInstanceFile).find(R"("version": "1.0.0")"), std::string::npos);

    auto err = mRootfsRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(RootfsRuntimeTest, InterruptedPromotionIsCompleted)
{
    // Power off after installed instance is updated but before pending instance is removed.

    WritePendingInstance("previousBootId", "1.0.1", true);
    WriteFile(cInstanceFile, ReadFile(cUpdateInstanceFile));
    WriteFile(cVersionFile, R"(VERSION="1.0.1")");
    WriteFile(cWorkingDir / "image.squashfs", "image");

    InitAndStart();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eActive);
    EXPECT_STREQ(statuses[0].mItemID.CStr(), "updateItemId");
    EXPECT_STREQ(statuses[0].mVersion.CStr(), "1.0.1");

    EXPECT_EQ(GetWorkingDirFiles(), (std::set {cInstanceFile}));

    auto err = mRootfsRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(RootfsRuntimeTest, DoUpdateSameBootRequestsReboot)
{
    // SM is restarted after update is prepared but before reboot.

    WritePendingInstance(GetBootID());
    WriteFile(cWorkingDir / "do_update", "full");
    WriteFile(cWorkingDir / "image.squashfs", "image");

    const auto cExpectedFiles = GetWorkingDirFiles();

    InitAndStart();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eActivating);
    EXPECT_STREQ(statuses[0].mItemID.CStr(), "updateItemId");

    EXPECT_TRUE(IsRebootRequested());
    EXPECT_EQ(GetWorkingDirFiles(), cExpectedFiles);

    auto status = std::make_unique<InstanceStatus>();

    auto err = mRootfsRuntime.StartInstance(
        *CreateInstance("updateItemId", "updateManifestDigest", "1.0.1", "updateSubjectId"), *status);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(status->mState, InstanceStateEnum::eActivating);
    EXPECT_STREQ(status->mSubjectID.CStr(), "updateSubjectId");

    err = mRootfsRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(RootfsRuntimeTest, DoUpdateAfterRebootIsFailed)
{
    // Reboot is done but initramfs didn't process the update.

    WritePendingInstance("previousBootId");
    WriteFile(cWorkingDir / "do_update", "full");
    WriteFile(cWorkingDir / "image.squashfs", "image");

    InitAndStart();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 2u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eFailed);
    EXPECT_STREQ(statuses[0].mItemID.CStr(), "updateItemId");
    EXPECT_STREQ(statuses[0].mError.Message(), "update is not processed after reboot");
    EXPECT_EQ(statuses[1].mState, InstanceStateEnum::eActive);
    EXPECT_STREQ(statuses[1].mItemID.CStr(), "itemId");

    EXPECT_FALSE(IsRebootRequested());
    EXPECT_EQ(GetWorkingDirFiles(), (std::set {cInstanceFile}));

    auto err = mRootfsRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(RootfsRuntimeTest, DoApplySameBootRequestsReboot)
{
    // SM is restarted after health check confirmed the update but before reboot.

    WritePendingInstance(GetBootID());
    WriteFile(cWorkingDir / "do_update", "full");
    WriteFile(cWorkingDir / "updated");
    WriteFile(cWorkingDir / "do_apply");
    WriteFile(cWorkingDir / "image.squashfs", "image");

    const auto cExpectedFiles = GetWorkingDirFiles();

    EXPECT_CALL(mSystemdConn, GetUnitStatus).Times(0);

    InitAndStart();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eActivating);
    EXPECT_STREQ(statuses[0].mItemID.CStr(), "updateItemId");

    EXPECT_TRUE(IsRebootRequested());
    EXPECT_EQ(GetWorkingDirFiles(), cExpectedFiles);

    // SM was interrupted before update was confirmed: confirmation is repaired.
    EXPECT_NE(ReadFile(cUpdateInstanceFile).find(R"("confirmed":true)"), std::string::npos);

    auto err = mRootfsRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(RootfsRuntimeTest, DoApplyAfterRebootIsFailed)
{
    // Initramfs failed to apply the update: rootfs may be partially updated, artifacts are kept to retry apply.

    WritePendingInstance("previousBootId", "1.0.1", true);
    WriteFile(cWorkingDir / "do_update", "full");
    WriteFile(cWorkingDir / "updated");
    WriteFile(cWorkingDir / "do_apply");
    WriteFile(cWorkingDir / "failed", "rsync failed");
    WriteFile(cWorkingDir / "image.squashfs", "image");

    const auto cExpectedFiles = GetWorkingDirFiles();

    EXPECT_CALL(mSystemdConn, GetUnitStatus).Times(0);

    InitAndStart();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eFailed);
    EXPECT_STREQ(statuses[0].mItemID.CStr(), "updateItemId");
    EXPECT_STREQ(statuses[0].mError.Message(), "rsync failed");

    EXPECT_FALSE(IsRebootRequested());
    EXPECT_EQ(GetWorkingDirFiles(), cExpectedFiles);

    auto err = mRootfsRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(RootfsRuntimeTest, DoApplyAfterRebootWithoutReasonIsFailed)
{
    WritePendingInstance("previousBootId", "1.0.1", true);
    WriteFile(cWorkingDir / "do_apply");
    WriteFile(cWorkingDir / "image.squashfs", "image");

    InitAndStart();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eFailed);
    EXPECT_STREQ(statuses[0].mError.Message(), "update is not applied after reboot");

    EXPECT_TRUE(std::filesystem::exists(cWorkingDir / "do_apply"));

    auto err = mRootfsRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(RootfsRuntimeTest, UpdatedAndFailedSameBootRequestsReboot)
{
    // SM is restarted after health check rejected the update but before reboot.

    WritePendingInstance(GetBootID());
    WriteFile(cWorkingDir / "do_update", "full");
    WriteFile(cWorkingDir / "updated");
    WriteFile(cWorkingDir / "failed", "health check failed");
    WriteFile(cWorkingDir / "image.squashfs", "image");

    const auto cExpectedFiles = GetWorkingDirFiles();

    EXPECT_CALL(mSystemdConn, GetUnitStatus).Times(0);

    InitAndStart();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eFailed);
    EXPECT_STREQ(statuses[0].mItemID.CStr(), "updateItemId");
    EXPECT_STREQ(statuses[0].mError.Message(), "health check failed");

    EXPECT_TRUE(IsRebootRequested());
    EXPECT_EQ(GetWorkingDirFiles(), cExpectedFiles);

    auto status = std::make_unique<InstanceStatus>();

    auto err = mRootfsRuntime.StartInstance(
        *CreateInstance("updateItemId", "updateManifestDigest", "1.0.1", "updateSubjectId"), *status);
    EXPECT_TRUE(err.Is(ErrorEnum::eFailed)) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(status->mState, InstanceStateEnum::eFailed);

    err = mRootfsRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(RootfsRuntimeTest, UpdatedVersionMismatchRevertsUpdate)
{
    // Trial boot is done, but rootfs version doesn't match the expected one.

    WritePendingInstance("previousBootId");
    WriteFile(cWorkingDir / "do_update", "full");
    WriteFile(cWorkingDir / "updated");
    WriteFile(cWorkingDir / "image.squashfs", "image");

    EXPECT_CALL(mSystemdConn, GetUnitStatus).Times(0);

    InitAndStart();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eFailed);
    EXPECT_STREQ(statuses[0].mItemID.CStr(), "updateItemId");
    EXPECT_STREQ(statuses[0].mError.Message(), "rootfs version mismatch");

    EXPECT_TRUE(IsRebootRequested());
    EXPECT_EQ(GetWorkingDirFiles(),
        (std::set {cInstanceFile, cUpdateInstanceFile, cWorkingDir / "do_update", cWorkingDir / "updated",
            cWorkingDir / "failed", cWorkingDir / "image.squashfs"}));
    EXPECT_EQ(ReadFile(cWorkingDir / "failed"), "rootfs version mismatch");
    EXPECT_NE(ReadFile(cUpdateInstanceFile).find(GetBootID()), std::string::npos);
    EXPECT_NE(ReadFile(cUpdateInstanceFile).find(R"("confirmed":false)"), std::string::npos);

    auto err = mRootfsRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(RootfsRuntimeTest, HealthCheckStoresBootID)
{
    WritePendingInstance("previousBootId");
    WriteFile(cWorkingDir / "do_update", "incremental");
    WriteFile(cWorkingDir / "updated");
    WriteFile(cWorkingDir / "image.squashfs", "image");
    WriteFile(cVersionFile, R"(VERSION="1.0.1")");

    InitAndStart();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eActivating);

    statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eActivating);

    EXPECT_TRUE(IsRebootRequested());

    auto err = mRootfsRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_TRUE(std::filesystem::exists(cWorkingDir / "do_apply"));
    EXPECT_NE(ReadFile(cUpdateInstanceFile).find(GetBootID()), std::string::npos);
    EXPECT_NE(ReadFile(cUpdateInstanceFile).find(R"("confirmed":true)"), std::string::npos);

    // SM restart within the same boot should not run health check again.

    EXPECT_CALL(mSystemdConn, GetUnitStatus).Times(0);

    err = mRootfsRuntime.Start();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eActivating);
    EXPECT_TRUE(IsRebootRequested());

    err = mRootfsRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(RootfsRuntimeTest, StaleArtifactsAreRemoved)
{
    std::filesystem::remove(cUpdateInstanceFile);

    WriteFile(cWorkingDir / "image.squashfs", "image");
    WriteFile(cWorkingDir / "image.squashfs.tmp", "image");
    WriteFile(cWorkingDir / "pending_instance.json.tmp", "{");
    WriteFile(cWorkingDir / "failed", "error");

    InitAndStart();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eActive);

    EXPECT_EQ(GetWorkingDirFiles(), (std::set {cInstanceFile}));

    auto err = mRootfsRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(RootfsRuntimeTest, StartInstanceInvalidImage)
{
    WriteFile(cUpdateRootfsFile, "rootfs image");

    const auto cLayerDigest = CalculateSHA256(cUpdateRootfsFile);
    const auto cLayerSize   = std::filesystem::file_size(cUpdateRootfsFile);

    struct TestCase {
        std::string mName;
        std::string mLayerDigest;
        std::string mMediaType;
        size_t      mSize;
        bool        mExpectBlob;
        ErrorEnum   mError;
    };

    const std::vector<TestCase> cTestCases = {
        {"digest mismatch", "sha256:" + std::string(64, '0'), "vnd.aos.image.component.full.v1", cLayerSize, true,
            ErrorEnum::eInvalidChecksum},
        {"size mismatch", cLayerDigest, "vnd.aos.image.component.inc.v1", cLayerSize + 1, true,
            ErrorEnum::eInvalidChecksum},
        {"zero size", cLayerDigest, "vnd.aos.image.component.inc.v1", 0, true, ErrorEnum::eInvalidChecksum},
        {"unsupported digest", "sha512:1234", "vnd.aos.image.component.full.v1", cLayerSize, false,
            ErrorEnum::eNotSupported},
        {"invalid digest", "1234", "vnd.aos.image.component.full.v1", cLayerSize, false, ErrorEnum::eInvalidArgument},
        {"unsupported media type", cLayerDigest, "vnd.aos.image.unknown", cLayerSize, false,
            ErrorEnum::eInvalidArgument},
    };

    InitAndStart();

    for (const auto& testCase : cTestCases) {
        LOG_INF() << "Test case" << Log::Field("name", testCase.mName.c_str());

        // Leftovers of previous interrupted update should be removed.
        WriteFile(cWorkingDir / "image.squashfs.tmp", "partial image");

        auto status = std::make_unique<InstanceStatus>();

        ExpectUpdateImage("rootfsImageDigest", testCase.mLayerDigest, testCase.mMediaType, testCase.mSize);

        if (testCase.mExpectBlob) {
            ExpectLayerBlob(testCase.mLayerDigest);
        }

        auto err = mRootfsRuntime.StartInstance(*CreateInstance("itemId", "rootfsImageDigest"), *status);
        EXPECT_TRUE(err.Is(testCase.mError)) << tests::utils::ErrorToStr(err);

        EXPECT_EQ(status->mState, InstanceStateEnum::eFailed);
        EXPECT_EQ(GetWorkingDirFiles(), (std::set {cInstanceFile}));
        EXPECT_FALSE(IsRebootRequested());
    }

    auto err = mRootfsRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(RootfsRuntimeTest, StartInstanceBlobNotFound)
{
    InitAndStart();

    const auto cLayerDigest = "sha256:" + std::string(64, '0');
    auto       status       = std::make_unique<InstanceStatus>();

    ExpectUpdateImage("rootfsImageDigest", cLayerDigest);
    ExpectLayerBlob(cLayerDigest, cTestDir / "notExist");

    auto err = mRootfsRuntime.StartInstance(*CreateInstance("itemId", "rootfsImageDigest"), *status);
    EXPECT_FALSE(err.IsNone());

    EXPECT_EQ(status->mState, InstanceStateEnum::eFailed);
    EXPECT_EQ(GetWorkingDirFiles(), (std::set {cInstanceFile}));

    status = std::make_unique<InstanceStatus>();

    EXPECT_CALL(mItemInfoProvider, GetBlobPath(StrEq("rootfsImageDigest"), _)).WillOnce(Return(ErrorEnum::eNotFound));

    err = mRootfsRuntime.StartInstance(*CreateInstance("itemId", "rootfsImageDigest"), *status);
    EXPECT_TRUE(err.Is(ErrorEnum::eNotFound)) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(status->mState, InstanceStateEnum::eFailed);

    status = std::make_unique<InstanceStatus>();

    EXPECT_CALL(mItemInfoProvider, GetBlobPath(StrEq("rootfsImageDigest"), _))
        .WillOnce(Invoke(SetPath(cUpdateRootfsManifestFile)));
    EXPECT_CALL(mOCISpec, LoadImageManifest).WillOnce(Return(ErrorEnum::eNone));

    err = mRootfsRuntime.StartInstance(*CreateInstance("itemId", "rootfsImageDigest"), *status);
    EXPECT_TRUE(err.Is(ErrorEnum::eInvalidArgument)) << tests::utils::ErrorToStr(err);

    err = mRootfsRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(RootfsRuntimeTest, StartInstanceReplacesPendingUpdate)
{
    WriteFile(cUpdateRootfsFile, "rootfs image 1");

    const auto cFirstDigest = CalculateSHA256(cUpdateRootfsFile);

    InitAndStart();

    auto status = std::make_unique<InstanceStatus>();

    ExpectUpdateImage(
        "firstManifest", cFirstDigest, "vnd.aos.image.component.inc.v1", std::filesystem::file_size(cUpdateRootfsFile));
    ExpectLayerBlob(cFirstDigest);

    auto err = mRootfsRuntime.StartInstance(*CreateInstance("itemId", "firstManifest"), *status);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
    EXPECT_EQ(ReadFile(cWorkingDir / "do_update"), "incremental");

    const auto cSecondFile = cTestDir / "rootfs.1.0.2";

    WriteFile(cSecondFile, "rootfs image 2");

    const auto cSecondDigest = CalculateSHA256(cSecondFile);

    ExpectUpdateImage(
        "secondManifest", cSecondDigest, "vnd.aos.image.component.full.v1", std::filesystem::file_size(cSecondFile));
    ExpectLayerBlob(cSecondDigest, cSecondFile);

    err = mRootfsRuntime.StartInstance(*CreateInstance("itemId", "secondManifest", "1.0.2"), *status);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(GetWorkingDirFiles(),
        (std::set {cInstanceFile, cUpdateInstanceFile, cWorkingDir / "image.squashfs", cWorkingDir / "do_update"}));
    EXPECT_EQ(ReadFile(cWorkingDir / "do_update"), "full");
    EXPECT_EQ(CalculateSHA256(cWorkingDir / "image.squashfs"), cSecondDigest);
    EXPECT_NE(ReadFile(cUpdateInstanceFile).find("secondManifest"), std::string::npos);

    err = mRootfsRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(RootfsRuntimeTest, StartInstanceWaitsForHealthCheck)
{
    WritePendingInstance("previousBootId");
    WriteFile(cWorkingDir / "do_update", "full");
    WriteFile(cWorkingDir / "updated");
    WriteFile(cWorkingDir / "image.squashfs", "image");
    WriteFile(cVersionFile, R"(VERSION="1.0.1")");
    WriteFile(cUpdateRootfsFile, "rootfs image");

    std::promise<void> checkPromise;
    auto               checkFuture = checkPromise.get_future().share();

    EXPECT_CALL(mSystemdConn, GetUnitStatus("sm")).WillOnce(Invoke([checkFuture](const auto&) {
        checkFuture.wait();

        sm::utils::UnitStatus status;

        status.mName        = "sm";
        status.mActiveState = sm::utils::UnitStateEnum::eActive;

        return RetWithError<sm::utils::UnitStatus>(status, ErrorEnum::eNone);
    }));

    InitAndStart();

    const auto cLayerDigest = CalculateSHA256(cUpdateRootfsFile);
    auto       status       = std::make_unique<InstanceStatus>();

    ExpectUpdateImage(
        "newManifest", cLayerDigest, "vnd.aos.image.component.full.v1", std::filesystem::file_size(cUpdateRootfsFile));
    ExpectLayerBlob(cLayerDigest);

    auto startFuture = std::async(std::launch::async,
        [&]() { return mRootfsRuntime.StartInstance(*CreateInstance("itemId", "newManifest", "1.0.2"), *status); });

    EXPECT_EQ(startFuture.wait_for(std::chrono::milliseconds(200)), std::future_status::timeout);

    checkPromise.set_value();

    auto err = startFuture.get();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    // New update replaces trial update artifacts.
    EXPECT_EQ(GetWorkingDirFiles(),
        (std::set {cInstanceFile, cUpdateInstanceFile, cWorkingDir / "image.squashfs", cWorkingDir / "do_update"}));
    EXPECT_NE(ReadFile(cUpdateInstanceFile).find("newManifest"), std::string::npos);

    err = mRootfsRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(RootfsRuntimeTest, StopInstance)
{
    InitAndStart();

    GetStatuses();

    auto status = std::make_unique<InstanceStatus>();

    auto err = mRootfsRuntime.StopInstance(*CreateInstance("itemId", "manifestDigest"), *status);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(status->mState, InstanceStateEnum::eInactive);

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eInactive);

    auto monitoringData = std::make_unique<monitoring::InstanceMonitoringData>();

    err = mRootfsRuntime.GetInstanceMonitoringData(*CreateInstance("itemId", "manifestDigest"), *monitoringData);
    EXPECT_TRUE(err.Is(ErrorEnum::eNotSupported));

    err = mRootfsRuntime.InitInstances(Array<InstanceInfo>());
    EXPECT_TRUE(err.IsNone());

    err = mRootfsRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(RootfsRuntimeTest, StartFailsOnLookupError)
{
    // Lookup errors should not be treated as missing files: otherwise state may be overwritten or unconfirmed update
    // may be promoted. Self referencing symlink causes ELOOP lookup error.

    struct TestCase {
        std::string           mName;
        std::filesystem::path mPath;
    };

    const std::vector<TestCase> cTestCases = {
        {"installed instance", cInstanceFile},
        {"pending instance", cUpdateInstanceFile},
        {"action", cWorkingDir / "updated"},
    };

    for (const auto& testCase : cTestCases) {
        LOG_INF() << "Test case" << Log::Field("name", testCase.mName.c_str());

        WriteFiles();
        WritePendingInstance(GetBootID(), "1.0.1", true);
        WriteFile(cVersionFile, R"(VERSION="1.0.1")");

        std::filesystem::remove(testCase.mPath);
        std::filesystem::create_symlink(testCase.mPath.filename(), testCase.mPath);

        auto err = mRootfsRuntime.Init(
            mConfig, mCurrentNodeInfoProvider, mItemInfoProvider, mOCISpec, mStatusReceiver, mSystemdConn);
        ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

        err = mRootfsRuntime.Start();
        EXPECT_FALSE(err.IsNone());

        EXPECT_TRUE(std::filesystem::is_symlink(testCase.mPath));
        EXPECT_FALSE(std::filesystem::exists(cWorkingDir / "do_apply"));

        std::filesystem::remove(testCase.mPath);
    }
}

TEST_F(RootfsRuntimeTest, UnconfirmedAppliedUpdateIsPromoted)
{
    // Power off after do_apply is stored but before update is confirmed: initramfs applied the update. Version change
    // proves that the update is applied.

    WritePendingInstance("previousBootId", "1.0.1", false);
    WriteFile(cVersionFile, R"(VERSION="1.0.1")");

    InitAndStart();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 2u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eInactive);
    EXPECT_STREQ(statuses[0].mVersion.CStr(), "1.0.0");
    EXPECT_EQ(statuses[1].mState, InstanceStateEnum::eActive);
    EXPECT_STREQ(statuses[1].mItemID.CStr(), "updateItemId");
    EXPECT_STREQ(statuses[1].mVersion.CStr(), "1.0.1");

    EXPECT_EQ(GetWorkingDirFiles(), (std::set {cInstanceFile}));

    auto err = mRootfsRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(RootfsRuntimeTest, RejectedUpdateWithDoApplyIsCleaned)
{
    // Health check rejected the update but do_apply removal failed before SM restart: do_apply should be removed,
    // otherwise initramfs applies rejected update.

    WritePendingInstance(GetBootID());
    WriteFile(cWorkingDir / "do_update", "full");
    WriteFile(cWorkingDir / "updated");
    WriteFile(cWorkingDir / "do_apply");
    WriteFile(cWorkingDir / "failed", "health check failed");
    WriteFile(cWorkingDir / "image.squashfs", "image");

    InitAndStart();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eFailed);
    EXPECT_STREQ(statuses[0].mError.Message(), "health check failed");

    EXPECT_TRUE(IsRebootRequested());
    EXPECT_FALSE(std::filesystem::exists(cWorkingDir / "do_apply"));
    EXPECT_TRUE(std::filesystem::exists(cWorkingDir / "failed"));

    auto err = mRootfsRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(RootfsRuntimeTest, StartTwiceWithHealthCheck)
{
    WritePendingInstance("previousBootId");
    WriteFile(cWorkingDir / "do_update", "full");
    WriteFile(cWorkingDir / "updated");
    WriteFile(cVersionFile, R"(VERSION="1.0.1")");

    InitAndStart();

    // Second start should join the health check thread of the first one instead of terminating.
    auto err = mRootfsRuntime.Start();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = mRootfsRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(RootfsRuntimeTest, StartInstanceFailsOnCleanupError)
{
    // Previous do_update can't be removed: new update artifacts should not be written and previous image should be
    // kept, otherwise initramfs finds do_update without image.

    InitAndStart();

    GetStatuses();

    std::filesystem::create_directories(cWorkingDir / "do_update" / "subdir");
    WriteFile(cWorkingDir / "image.squashfs", "image");

    auto status = std::make_unique<InstanceStatus>();

    auto err = mRootfsRuntime.StartInstance(*CreateInstance("itemId", "rootfsImageDigest"), *status);
    EXPECT_FALSE(err.IsNone());

    EXPECT_EQ(status->mState, InstanceStateEnum::eFailed);
    EXPECT_EQ(
        GetWorkingDirFiles(), (std::set {cInstanceFile, cWorkingDir / "do_update", cWorkingDir / "image.squashfs"}));
    EXPECT_FALSE(IsRebootRequested());

    err = mRootfsRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(RootfsRuntimeTest, NotConfirmedSameVersionIsFailed)
{
    // Power off after update with the same version but different image is prepared and before do_update is stored:
    // rootfs version matches, but the update is not applied.

    WritePendingInstance(GetBootID(), "1.0.0", false, "rebuiltManifestDigest");
    WriteFile(cWorkingDir / "image.squashfs", "image");

    InitAndStart();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 2u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eFailed);
    EXPECT_STREQ(statuses[0].mManifestDigest.CStr(), "rebuiltManifestDigest");
    EXPECT_STREQ(statuses[0].mError.Message(), "update is not confirmed");
    EXPECT_EQ(statuses[1].mState, InstanceStateEnum::eActive);
    EXPECT_STREQ(statuses[1].mManifestDigest.CStr(), "manifestDigest");

    EXPECT_EQ(GetWorkingDirFiles(), (std::set {cInstanceFile}));
    EXPECT_NE(ReadFile(cInstanceFile).find(R"("manifestDigest": "manifestDigest")"), std::string::npos);

    auto err = mRootfsRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(RootfsRuntimeTest, ConfirmedSameVersionIsPromoted)
{
    WritePendingInstance("previousBootId", "1.0.0", true, "rebuiltManifestDigest");

    InitAndStart();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 2u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eInactive);
    EXPECT_STREQ(statuses[0].mManifestDigest.CStr(), "manifestDigest");
    EXPECT_EQ(statuses[1].mState, InstanceStateEnum::eActive);
    EXPECT_STREQ(statuses[1].mManifestDigest.CStr(), "rebuiltManifestDigest");

    EXPECT_EQ(GetWorkingDirFiles(), (std::set {cInstanceFile}));

    auto err = mRootfsRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(RootfsRuntimeTest, StartInstanceMediaTypes)
{
    WriteFile(cUpdateRootfsFile, "rootfs image");

    const auto cLayerDigest = CalculateSHA256(cUpdateRootfsFile);
    const auto cLayerSize   = std::filesystem::file_size(cUpdateRootfsFile);

    const std::vector<std::pair<std::string, std::string>> cValidMediaTypes = {
        {"application/vnd.aos.image.component.full.v1+squashfs", "full"},
        {"application/vnd.aos.image.component.inc.v1+squashfs", "incremental"},
        {"vnd.aos.image.component.full.v12", "full"},
        {"vnd.aos.image.component.inc.v1+gzip", "incremental"},
    };

    const std::vector<std::string> cInvalidMediaTypes = {
        "vnd.aos.image.component.fullness.v1",
        "vnd.aos.image.component.full",
        "vnd.aos.image.component.full.v",
        "vnd.aos.image.component.full.vx",
        "vnd.aos.image.component.inc.v1x+squashfs",
        "application/x.vnd.aos.image.component.full.v1",
        "text/vnd.aos.image.component.full.v1",
    };

    InitAndStart();

    for (size_t i = 0; i < cValidMediaTypes.size(); ++i) {
        const auto& [mediaType, updateType] = cValidMediaTypes[i];
        const auto manifestDigest           = "manifest" + std::to_string(i);

        LOG_INF() << "Valid media type" << Log::Field("mediaType", mediaType.c_str());

        auto status = std::make_unique<InstanceStatus>();

        ExpectUpdateImage(manifestDigest, cLayerDigest, mediaType, cLayerSize);
        ExpectLayerBlob(cLayerDigest);

        auto err = mRootfsRuntime.StartInstance(*CreateInstance("itemId", manifestDigest), *status);
        ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

        EXPECT_EQ(ReadFile(cWorkingDir / "do_update"), updateType);
    }

    for (const auto& mediaType : cInvalidMediaTypes) {
        LOG_INF() << "Invalid media type" << Log::Field("mediaType", mediaType.c_str());

        auto status = std::make_unique<InstanceStatus>();

        ExpectUpdateImage("invalidManifest", cLayerDigest, mediaType, cLayerSize);

        auto err = mRootfsRuntime.StartInstance(*CreateInstance("itemId", "invalidManifest"), *status);
        EXPECT_TRUE(err.Is(ErrorEnum::eInvalidArgument)) << tests::utils::ErrorToStr(err);

        EXPECT_EQ(status->mState, InstanceStateEnum::eFailed);
        EXPECT_EQ(GetWorkingDirFiles(), (std::set {cInstanceFile}));
    }

    auto err = mRootfsRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(RootfsRuntimeTest, ConfirmedVersionMismatchIsFailed)
{
    // Confirmed update without action files, but rootfs version doesn't match: the update is not applied.

    WritePendingInstance("previousBootId", "1.0.1", true);

    InitAndStart();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 2u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eFailed);
    EXPECT_STREQ(statuses[0].mError.Message(), "rootfs version mismatch");
    EXPECT_EQ(statuses[1].mState, InstanceStateEnum::eActive);
    EXPECT_STREQ(statuses[1].mVersion.CStr(), "1.0.0");

    EXPECT_EQ(GetWorkingDirFiles(), (std::set {cInstanceFile}));

    auto err = mRootfsRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(RootfsRuntimeTest, CleanupStopsIfPendingInstanceCantBeRemoved)
{
    // Action files should not be removed while pending instance remains: otherwise failed update may be promoted.

    std::filesystem::remove(cUpdateInstanceFile);
    std::filesystem::create_directories(cUpdateInstanceFile / "subdir");
    WriteFile(cWorkingDir / "failed", "error");
    WriteFile(cWorkingDir / "do_apply");

    InitAndStart();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eActive);

    EXPECT_EQ(GetWorkingDirFiles(),
        (std::set {cInstanceFile, cUpdateInstanceFile, cWorkingDir / "failed", cWorkingDir / "do_apply"}));

    auto err = mRootfsRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(RootfsRuntimeTest, RebootRequestIsRetried)
{
    WritePendingInstance(GetBootID());
    WriteFile(cWorkingDir / "do_update", "full");
    WriteFile(cWorkingDir / "image.squashfs", "image");

    mStatusReceiver.mFailReboot = true;

    InitAndStart();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eActivating);
    EXPECT_TRUE(statuses[0].mError.Is(ErrorEnum::eFailed)) << tests::utils::ErrorToStr(statuses[0].mError);
    EXPECT_FALSE(IsRebootRequested());

    mStatusReceiver.mFailReboot = false;

    auto status = std::make_unique<InstanceStatus>();

    auto err = mRootfsRuntime.StartInstance(
        *CreateInstance("updateItemId", "updateManifestDigest", "1.0.1", "updateSubjectId"), *status);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(status->mState, InstanceStateEnum::eActivating);
    EXPECT_TRUE(status->mError.IsNone()) << tests::utils::ErrorToStr(status->mError);
    EXPECT_TRUE(IsRebootRequested());

    err = mRootfsRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(RootfsRuntimeTest, RevertRebootRequestErrorIsReported)
{
    WriteFile(cUpdateInstanceFile, "");
    WriteFile(cWorkingDir / "updated");

    mStatusReceiver.mFailReboot = true;

    InitAndStart();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eActive);
    EXPECT_TRUE(statuses[0].mError.Is(ErrorEnum::eFailed)) << tests::utils::ErrorToStr(statuses[0].mError);

    auto err = mRootfsRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(RootfsRuntimeTest, HealthCheckRebootRequestErrorIsReported)
{
    WritePendingInstance("previousBootId");
    WriteFile(cWorkingDir / "do_update", "full");
    WriteFile(cWorkingDir / "updated");
    WriteFile(cWorkingDir / "image.squashfs", "image");
    WriteFile(cVersionFile, R"(VERSION="1.0.1")");

    mStatusReceiver.mFailReboot = true;

    InitAndStart();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eActivating);

    statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eActivating);
    EXPECT_TRUE(statuses[0].mError.Is(ErrorEnum::eFailed)) << tests::utils::ErrorToStr(statuses[0].mError);

    auto err = mRootfsRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_TRUE(std::filesystem::exists(cWorkingDir / "do_apply"));

    // Reboot request is retried on start instance request.

    mStatusReceiver.mFailReboot = false;

    auto status = std::make_unique<InstanceStatus>();

    err = mRootfsRuntime.StartInstance(
        *CreateInstance("updateItemId", "updateManifestDigest", "1.0.1", "updateSubjectId"), *status);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_TRUE(IsRebootRequested());
}

TEST_F(RootfsRuntimeTest, RejectedUpdateWithStuckDoApplyPostponesReboot)
{
    // Health check rejected the update but do_apply can't be removed: reboot should not be requested, otherwise
    // initramfs applies rejected update.

    WritePendingInstance(GetBootID());
    WriteFile(cWorkingDir / "do_update", "full");
    WriteFile(cWorkingDir / "updated");
    std::filesystem::create_directories(cWorkingDir / "do_apply" / "subdir");
    WriteFile(cWorkingDir / "failed", "health check failed");
    WriteFile(cWorkingDir / "image.squashfs", "image");

    InitAndStart();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eFailed);
    EXPECT_STREQ(statuses[0].mError.Message(), "health check failed");

    EXPECT_FALSE(IsRebootRequested());
    EXPECT_TRUE(std::filesystem::exists(cWorkingDir / "do_apply"));

    // Reboot should not be requested on start instance request either.

    auto status = std::make_unique<InstanceStatus>();

    auto err = mRootfsRuntime.StartInstance(
        *CreateInstance("updateItemId", "updateManifestDigest", "1.0.1", "updateSubjectId"), *status);
    EXPECT_TRUE(err.Is(ErrorEnum::eFailed)) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(status->mState, InstanceStateEnum::eFailed);
    EXPECT_FALSE(IsRebootRequested());

    err = mRootfsRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(RootfsRuntimeTest, CorruptedPendingWithStuckDoApplyPostponesReboot)
{
    // Pending instance is corrupted during trial boot and do_apply can't be removed: reboot should not be requested,
    // otherwise initramfs applies update which can't be tracked.

    WriteFile(cUpdateInstanceFile, "");
    WriteFile(cWorkingDir / "do_update", "full");
    WriteFile(cWorkingDir / "updated");
    std::filesystem::create_directories(cWorkingDir / "do_apply" / "subdir");
    WriteFile(cWorkingDir / "image.squashfs", "image");

    InitAndStart();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eActive);
    EXPECT_STREQ(statuses[0].mItemID.CStr(), "itemId");
    EXPECT_FALSE(statuses[0].mError.IsNone());

    EXPECT_FALSE(IsRebootRequested());
    EXPECT_TRUE(std::filesystem::exists(cWorkingDir / "do_apply"));
    EXPECT_FALSE(std::filesystem::exists(cWorkingDir / "failed"));

    auto err = mRootfsRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(RootfsRuntimeTest, StartInstanceKeepsUpdateOnRebootRequestError)
{
    WriteFile(cUpdateRootfsFile, "rootfs image");

    const auto cLayerDigest = CalculateSHA256(cUpdateRootfsFile);

    InitAndStart();

    GetStatuses();

    mStatusReceiver.mFailReboot = true;

    auto status = std::make_unique<InstanceStatus>();

    ExpectUpdateImage(
        "newManifest", cLayerDigest, "vnd.aos.image.component.full.v1", std::filesystem::file_size(cUpdateRootfsFile));
    ExpectLayerBlob(cLayerDigest);

    auto err = mRootfsRuntime.StartInstance(*CreateInstance("itemId", "newManifest"), *status);
    EXPECT_TRUE(err.Is(ErrorEnum::eFailed)) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(status->mState, InstanceStateEnum::eActivating);
    EXPECT_TRUE(status->mError.Is(ErrorEnum::eFailed));
    EXPECT_EQ(GetWorkingDirFiles(),
        (std::set {cInstanceFile, cUpdateInstanceFile, cWorkingDir / "image.squashfs", cWorkingDir / "do_update"}));

    // Prepared update is kept, reboot request is retried without preparing the update again.

    mStatusReceiver.mFailReboot = false;

    status = std::make_unique<InstanceStatus>();

    err = mRootfsRuntime.StartInstance(*CreateInstance("itemId", "newManifest"), *status);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(status->mState, InstanceStateEnum::eActivating);
    EXPECT_TRUE(IsRebootRequested());

    err = mRootfsRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
}

TEST_F(RootfsRuntimeTest, HealthCheckVerdictStoreErrorPostponesReboot)
{
    // do_apply is stored but the verdict can't be completed: reboot would apply the update reported as failed.

    WritePendingInstance("previousBootId");
    WriteFile(cWorkingDir / "do_update", "full");
    WriteFile(cWorkingDir / "updated");
    WriteFile(cWorkingDir / "image.squashfs", "image");
    WriteFile(cVersionFile, R"(VERSION="1.0.1")");

    EXPECT_CALL(mSystemdConn, GetUnitStatus("sm")).WillOnce(Invoke([](const auto&) {
        WriteFile(cWorkingDir / "do_apply");
        std::filesystem::create_directories(cWorkingDir / "pending_instance.json.tmp" / "subdir");

        sm::utils::UnitStatus status;

        status.mName        = "sm";
        status.mActiveState = sm::utils::UnitStateEnum::eActive;

        return RetWithError<sm::utils::UnitStatus>(status, ErrorEnum::eNone);
    }));

    InitAndStart();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eActivating);

    statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eFailed);

    auto err = mRootfsRuntime.Stop();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_FALSE(IsRebootRequested());
    EXPECT_TRUE(std::filesystem::exists(cWorkingDir / "do_apply"));

    std::filesystem::remove_all(cWorkingDir / "pending_instance.json.tmp");
}

} // namespace aos::sm::launcher
