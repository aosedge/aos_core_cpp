/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>

#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <Poco/DigestEngine.h>
#include <Poco/DigestStream.h>
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

/***********************************************************************************************************************
 * File operations fault injection
 **********************************************************************************************************************/

namespace {

using FaultPredicate = std::function<bool(const std::filesystem::path&)>;

std::mutex     sFaultMutex;
FaultPredicate sUnlinkFault;
FaultPredicate sFsyncFault;
int            sFsyncErrno = EIO;

FaultPredicate GetFault(const FaultPredicate& fault)
{
    std::lock_guard lock {sFaultMutex};

    return fault;
}

int GetFsyncErrno()
{
    std::lock_guard lock {sFaultMutex};

    return sFsyncErrno;
}

void SetFaults(FaultPredicate unlinkFault, FaultPredicate fsyncFault = nullptr, int fsyncErrno = EIO)
{
    std::lock_guard lock {sFaultMutex};

    sUnlinkFault = std::move(unlinkFault);
    sFsyncFault  = std::move(fsyncFault);
    sFsyncErrno  = fsyncErrno;
}

} // namespace

// File operations are intercepted to inject errors which can't be reproduced with file permissions: unlink error of a
// regular file regardless of the user, directory sync error after successful unlink. Predicates receive absolute path.
extern "C" int unlink(const char* path) noexcept
{
    if (auto fault = GetFault(sUnlinkFault); fault) {
        std::error_code ec;

        if (const auto absPath = std::filesystem::absolute(path, ec); !ec && fault(absPath)) {
            errno = EIO;

            return -1;
        }
    }

    return static_cast<int>(syscall(SYS_unlinkat, AT_FDCWD, path, 0));
}

extern "C" int fsync(int fd)
{
    if (auto fault = GetFault(sFsyncFault); fault) {
        std::error_code ec;

        if (const auto path = std::filesystem::read_symlink("/proc/self/fd/" + std::to_string(fd), ec);
            !ec && fault(path)) {
            errno = GetFsyncErrno();

            return -1;
        }
    }

    return static_cast<int>(syscall(SYS_fsync, fd));
}

namespace aos::sm::launcher {

namespace {

/***********************************************************************************************************************
 * Consts
 **********************************************************************************************************************/

const auto cTestDir                  = std::filesystem::path("testRootfs");
const auto cUncompressedTestFile     = cTestDir / "testfile.1.0.1.squashfs";
const auto cWorkingDir               = cTestDir / "workdir";
const auto cStateFile                = cWorkingDir / "update_state";
const auto cImageFile                = cWorkingDir / "image.squashfs";
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

// In-memory runtime storage with error injection.
class RuntimeStorageFake : public runtimes::StorageItf {
public:
    Error AddRuntimeInstance(const runtimes::RuntimeInstanceData& instance) override
    {
        std::lock_guard lock {mMutex};

        if (mFailWrite) {
            return ErrorEnum::eFailed;
        }

        if (Find(instance.mRuntimeID, instance, instance.mVersion) != mInstances.end()) {
            return ErrorEnum::eAlreadyExist;
        }

        mInstances.push_back(instance);

        return ErrorEnum::eNone;
    }

    Error UpdateRuntimeInstance(const runtimes::RuntimeInstanceData& instance) override
    {
        std::lock_guard lock {mMutex};

        if (mFailWrite || mFailUpdate) {
            return ErrorEnum::eFailed;
        }

        auto it = Find(instance.mRuntimeID, instance, instance.mVersion);
        if (it == mInstances.end()) {
            return ErrorEnum::eNotFound;
        }

        *it = instance;

        return ErrorEnum::eNone;
    }

    Error RemoveRuntimeInstance(const String& runtimeID, const InstanceIdent& ident, const String& version) override
    {
        std::lock_guard lock {mMutex};

        if (mFailWrite || mFailRemove) {
            return ErrorEnum::eFailed;
        }

        auto it = Find(runtimeID, ident, version);
        if (it == mInstances.end()) {
            return ErrorEnum::eNotFound;
        }

        mInstances.erase(it);

        return ErrorEnum::eNone;
    }

    Error GetRuntimeInstances(const String& runtimeID, Array<runtimes::RuntimeInstanceData>& instances) const override
    {
        std::lock_guard lock {mMutex};

        if (mFailRead) {
            return ErrorEnum::eFailed;
        }

        for (const auto& instance : mInstances) {
            if (instance.mRuntimeID != runtimeID) {
                continue;
            }

            if (auto err = instances.PushBack(instance); !err.IsNone()) {
                return err;
            }
        }

        return ErrorEnum::eNone;
    }

    std::vector<runtimes::RuntimeInstanceData> GetInstances() const
    {
        std::lock_guard lock {mMutex};

        return mInstances;
    }

    std::optional<runtimes::RuntimeInstanceData> GetInstance(const std::string& manifestDigest) const
    {
        std::lock_guard lock {mMutex};

        for (const auto& instance : mInstances) {
            if (manifestDigest == instance.mManifestDigest.CStr()) {
                return instance;
            }
        }

        return std::nullopt;
    }

    void SetInstances(std::vector<runtimes::RuntimeInstanceData> instances)
    {
        std::lock_guard lock {mMutex};

        mInstances = std::move(instances);
    }

    std::atomic_bool mFailWrite {};
    std::atomic_bool mFailRead {};
    std::atomic_bool mFailUpdate {};
    std::atomic_bool mFailRemove {};

private:
    // Matches the database primary key: the preinstalled flag is not part of it.
    std::vector<runtimes::RuntimeInstanceData>::iterator Find(
        const String& runtimeID, const InstanceIdent& ident, const String& version)
    {
        return std::find_if(mInstances.begin(), mInstances.end(), [&](const auto& instance) {
            return instance.mRuntimeID == runtimeID && instance.mItemID == ident.mItemID
                && instance.mSubjectID == ident.mSubjectID && instance.mInstance == ident.mInstance
                && instance.mType == ident.mType && instance.mVersion == version;
        });
    }

    mutable std::mutex                         mMutex;
    std::vector<runtimes::RuntimeInstanceData> mInstances;
};

// Calculates real file size and SHA256 hash.
class FileInfoProviderFake : public fs::FileInfoProviderItf {
public:
    Error GetFileInfo(const String& path, fs::FileInfo& info, const crypto::Hash& hashAlg) override
    {
        if (mFail || hashAlg != crypto::HashEnum::eSHA256) {
            return ErrorEnum::eFailed;
        }

        std::error_code ec;

        info.mSize = std::filesystem::file_size(path.CStr(), ec);
        if (ec) {
            return ErrorEnum::eNotFound;
        }

        const auto digest = CalculateSHA256(path.CStr());

        return String(digest.c_str() + digest.find(':') + 1).HexToByteArray(info.mCheckSum);
    }

    std::atomic_bool mFail {};
};

std::string GetExpectedRuntimeID()
{
    return Poco::UUIDGenerator::defaultGenerator().createFromName(Poco::UUID::oid(), "rootfs-nodeId").toString();
}

runtimes::RuntimeInstanceData CreateRuntimeInstance(const std::string& itemID, const std::string& subjectID,
    const std::string& manifestDigest, const std::string& version, InstanceState state, const Error& error = {},
    bool preinstalled = false)
{
    runtimes::RuntimeInstanceData instance;

    instance.mItemID         = itemID.c_str();
    instance.mSubjectID      = subjectID.c_str();
    instance.mType           = UpdateItemTypeEnum::eComponent;
    instance.mPreinstalled   = preinstalled;
    instance.mRuntimeID      = GetExpectedRuntimeID().c_str();
    instance.mManifestDigest = manifestDigest.c_str();
    instance.mVersion        = version.c_str();
    instance.mState          = state;
    instance.mError          = error;

    return instance;
}

runtimes::RuntimeInstanceData CreateCurrent()
{
    return CreateRuntimeInstance(
        "itemId", "subjectId", "manifestDigest", "1.0.0", InstanceStateEnum::eActive, {}, true);
}

runtimes::RuntimeInstanceData CreatePending(
    InstanceState state = InstanceStateEnum::eActivating, const Error& error = {}, const std::string& version = "1.0.1")
{
    return CreateRuntimeInstance("updateItemId", "updateSubjectId", "updateManifestDigest", version, state, error);
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

        ExpectUnitState(sm::utils::UnitStateEnum::eActive);

        WriteFile(cVersionFile, R"(VERSION="1.0.0")");
        WriteFile(cUpdateRootfsFile, "dummy rootfs content");

        mStorage.SetInstances({CreateCurrent()});

        EXPECT_CALL(mCurrentNodeInfoProvider, GetCurrentNodeInfo(_)).WillRepeatedly(Invoke([](NodeInfo& nodeInfo) {
            nodeInfo.mNodeID   = "nodeId";
            nodeInfo.mNodeType = "nodeType";

            nodeInfo.mCPUs.EmplaceBack();
            nodeInfo.mCPUs[0].mArchInfo.mArchitecture = "amd64";

            nodeInfo.mOSInfo.mOS = "linux";

            return ErrorEnum::eNone;
        }));
    }

    void TearDown() override { SetFaults(nullptr); }

    void WriteState(const std::string& state, const std::string& reason = "")
    {
        WriteFile(cStateFile, "state=" + state + "\ntype=full\nreason=" + reason + "\n");
        WriteFile(cImageFile, "image");
    }

    static std::string ReadStateValue(const std::string& key)
    {
        std::ifstream file(cStateFile);
        std::string   line;

        while (std::getline(file, line)) {
            if (line.rfind(key + "=", 0) == 0) {
                return line.substr(key.size() + 1);
            }
        }

        return "";
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

    // Expects update with valid layer blob of the update rootfs file.
    void ExpectValidUpdate(
        const std::string& manifestDigest, const std::string& mediaType = "vnd.aos.image.component.full.v1")
    {
        const auto layerDigest = CalculateSHA256(cUpdateRootfsFile);

        ExpectUpdateImage(manifestDigest, layerDigest, mediaType, std::filesystem::file_size(cUpdateRootfsFile));
        ExpectLayerBlob(layerDigest);
    }

    void ExpectLayerBlob(const std::string& layerDigest, const std::filesystem::path& path = cUpdateRootfsFile)
    {
        EXPECT_CALL(mItemInfoProvider, GetBlobPath(StrEq(layerDigest), _)).WillOnce(Invoke(SetPath(path)));
    }

    void ExpectUnitState(sm::utils::UnitStateEnum state)
    {
        EXPECT_CALL(mSystemdConn, GetUnitStatus("sm")).WillRepeatedly(Invoke([state](const auto&) {
            sm::utils::UnitStatus status;

            status.mName        = "sm";
            status.mActiveState = state;

            return RetWithError<sm::utils::UnitStatus>(status, ErrorEnum::eNone);
        }));
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

    std::unique_ptr<InstanceInfo> CreatePendingInstance() const
    {
        return CreateInstance("updateItemId", "updateManifestDigest", "1.0.1", "updateSubjectId");
    }

    std::unique_ptr<InstanceInfo> CreateCurrentInstance() const
    {
        auto instance = CreateInstance("itemId", "manifestDigest", "1.0.0");

        instance->mPreinstalled = true;

        return instance;
    }

    void InitAndStart()
    {
        auto err = mRootfsRuntime.Init(mConfig, mCurrentNodeInfoProvider, mItemInfoProvider, mOCISpec, mStatusReceiver,
            mSystemdConn, mStorage, mFileInfoProvider);
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

    void Stop()
    {
        auto err = mRootfsRuntime.Stop();
        ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
    }

    RuntimeConfig                          mConfig;
    iamclient::CurrentNodeInfoProviderMock mCurrentNodeInfoProvider;
    imagemanager::ItemInfoProviderMock     mItemInfoProvider;
    oci::OCISpecMock                       mOCISpec;
    InstanceStatusReceiverFailingStub      mStatusReceiver;
    sm::utils::SystemdConnMock             mSystemdConn;
    RuntimeStorageFake                     mStorage;
    FileInfoProviderFake                   mFileInfoProvider;
    RootfsRuntime                          mRootfsRuntime;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(RootfsRuntimeTest, GetRuntimeInfo)
{
    InitAndStart();

    auto info = std::make_unique<RuntimeInfo>();

    auto err = mRootfsRuntime.GetRuntimeInfo(*info);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_STREQ(info->mRuntimeType.CStr(), "rootfs");
    EXPECT_EQ(info->mMaxInstances, 1u);
    EXPECT_STREQ(info->mRuntimeID.CStr(), GetExpectedRuntimeID().c_str());
    EXPECT_STREQ(info->mArchInfo.mArchitecture.CStr(), "amd64");
    EXPECT_STREQ(info->mOSInfo.mOS.CStr(), "linux");

    Stop();
}

TEST_F(RootfsRuntimeTest, NoUpdate)
{
    InitAndStart();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eActive);
    EXPECT_STREQ(statuses[0].mItemID.CStr(), "itemId");
    EXPECT_STREQ(statuses[0].mSubjectID.CStr(), "subjectId");
    EXPECT_STREQ(statuses[0].mManifestDigest.CStr(), "manifestDigest");
    EXPECT_STREQ(statuses[0].mVersion.CStr(), "1.0.0");
    EXPECT_TRUE(statuses[0].mPreinstalled);

    EXPECT_TRUE(GetWorkingDirFiles().empty());
    EXPECT_EQ(mStorage.GetInstances(), std::vector {CreateCurrent()});
    EXPECT_FALSE(IsRebootRequested());

    Stop();
}

TEST_F(RootfsRuntimeTest, StaleArtifactsAreRemoved)
{
    WriteFile(cImageFile, "image");
    WriteFile(cWorkingDir / "image.squashfs.tmp", "image");
    WriteFile(cWorkingDir / "update_state.tmp", "state=confirmed");

    InitAndStart();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eActive);

    EXPECT_TRUE(GetWorkingDirFiles().empty());

    Stop();
}

TEST_F(RootfsRuntimeTest, DefaultCurrentInstanceIsCreated)
{
    mStorage.SetInstances({});

    InitAndStart();

    auto onStartStatuses = GetStatuses();

    ASSERT_EQ(onStartStatuses.size(), 1u);
    EXPECT_EQ(onStartStatuses[0].mState, InstanceStateEnum::eActive);
    EXPECT_STREQ(onStartStatuses[0].mItemID.CStr(), "rootfs");
    EXPECT_STREQ(onStartStatuses[0].mSubjectID.CStr(), "nodeType");
    EXPECT_STREQ(onStartStatuses[0].mVersion.CStr(), "1.0.0");
    EXPECT_TRUE(onStartStatuses[0].mPreinstalled);

    const auto stored = mStorage.GetInstances();

    ASSERT_EQ(stored.size(), 1u);
    EXPECT_STREQ(stored[0].mItemID.CStr(), "rootfs");
    EXPECT_EQ(stored[0].mState, InstanceStateEnum::eActive);

    // Start instance request for the current instance reports it as active.

    auto status   = std::make_unique<InstanceStatus>();
    auto instance = std::make_unique<InstanceInfo>();

    static_cast<InstanceIdent&>(*instance) = static_cast<const InstanceIdent&>(onStartStatuses[0]);
    instance->mVersion                     = onStartStatuses[0].mVersion;

    auto err = mRootfsRuntime.StartInstance(*instance, *status);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
    EXPECT_EQ(onStartStatuses[0], *status);
    EXPECT_EQ(onStartStatuses, GetStatuses());

    Stop();
}

TEST_F(RootfsRuntimeTest, StartFails)
{
    // Without current instance and version file.

    mStorage.SetInstances({});
    std::filesystem::remove(cVersionFile);

    auto err = mRootfsRuntime.Init(mConfig, mCurrentNodeInfoProvider, mItemInfoProvider, mOCISpec, mStatusReceiver,
        mSystemdConn, mStorage, mFileInfoProvider);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = mRootfsRuntime.Start();
    EXPECT_TRUE(err.Is(ErrorEnum::eNotFound)) << tests::utils::ErrorToStr(err);

    WriteFile(cVersionFile, "invalid");

    err = mRootfsRuntime.Start();
    EXPECT_TRUE(err.Is(ErrorEnum::eInvalidArgument)) << tests::utils::ErrorToStr(err);

    // Storage read error.

    WriteFile(cVersionFile, R"(VERSION="1.0.0")");
    mStorage.mFailRead = true;

    err = mRootfsRuntime.Start();
    EXPECT_FALSE(err.IsNone());

    mStorage.mFailRead = false;

    // State file lookup error should not be treated as missing file. Self referencing symlink causes ELOOP.

    mStorage.SetInstances({CreateCurrent(), CreatePending()});
    WriteFile(cImageFile, "image");
    std::filesystem::create_symlink(cStateFile.filename(), cStateFile);

    err = mRootfsRuntime.Start();
    EXPECT_FALSE(err.IsNone());

    EXPECT_TRUE(std::filesystem::is_symlink(cStateFile));
    EXPECT_TRUE(std::filesystem::exists(cImageFile));
}

TEST_F(RootfsRuntimeTest, StartInstance)
{
    CreateGZIP(cUpdateRootfsFile.c_str());

    InitAndStart();

    GetStatuses();

    auto instance = CreateInstance("itemId", "rootfsImageDigest");
    auto status   = std::make_unique<InstanceStatus>();

    ExpectValidUpdate("rootfsImageDigest", "vnd.aos.image.component.full.v1+gzip");

    auto err = mRootfsRuntime.StartInstance(*instance, *status);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(status->mState, InstanceStateEnum::eActivating);

    EXPECT_EQ(GetWorkingDirFiles(), (std::set {cStateFile, cImageFile}));
    EXPECT_EQ(CalculateSHA256(cImageFile), CalculateSHA256(cUpdateRootfsFile));
    EXPECT_EQ(ReadFile(cStateFile), "state=prepared\ntype=full\nreason=\n");

    const auto pending = mStorage.GetInstance("rootfsImageDigest");

    ASSERT_TRUE(pending.has_value());
    EXPECT_STREQ(pending->mItemID.CStr(), "itemId");
    EXPECT_STREQ(pending->mVersion.CStr(), "1.0.1");
    EXPECT_EQ(pending->mState, InstanceStateEnum::eActivating);
    EXPECT_EQ(mStorage.GetInstances().size(), 2u);

    std::vector<StaticString<cIDLen>> rebootRuntimes;

    err = mStatusReceiver.GetRuntimesToReboot(rebootRuntimes, std::chrono::seconds(1));
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    ASSERT_EQ(rebootRuntimes.size(), 1u);
    EXPECT_STREQ(rebootRuntimes[0].CStr(), GetExpectedRuntimeID().c_str());

    // Start the same instance again: update is pending, no new update should be prepared.

    err = mRootfsRuntime.StartInstance(*instance, *status);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(status->mState, InstanceStateEnum::eActivating);

    Stop();
}

TEST_F(RootfsRuntimeTest, StartInstanceMediaTypes)
{
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

        EXPECT_EQ(ReadStateValue("type"), updateType);

        // The previous pending instance is replaced.
        EXPECT_EQ(mStorage.GetInstances().size(), 2u);
    }

    const auto cLastManifest = "manifest" + std::to_string(cValidMediaTypes.size() - 1);

    for (const auto& mediaType : cInvalidMediaTypes) {
        LOG_INF() << "Invalid media type" << Log::Field("mediaType", mediaType.c_str());

        auto status = std::make_unique<InstanceStatus>();

        ExpectUpdateImage("invalidManifest", cLayerDigest, mediaType, cLayerSize);

        auto err = mRootfsRuntime.StartInstance(*CreateInstance("itemId", "invalidManifest"), *status);
        EXPECT_TRUE(err.Is(ErrorEnum::eInvalidArgument)) << tests::utils::ErrorToStr(err);

        EXPECT_EQ(status->mState, InstanceStateEnum::eFailed);

        // The previous update is kept.
        EXPECT_EQ(GetWorkingDirFiles(), (std::set {cStateFile, cImageFile}));
        EXPECT_EQ(ReadStateValue("state"), "prepared");
        EXPECT_EQ(mStorage.GetInstances().size(), 2u);
        EXPECT_TRUE(mStorage.GetInstance(cLastManifest).has_value());
    }

    Stop();
}

TEST_F(RootfsRuntimeTest, StartInstanceInvalidImage)
{
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

    // Leftovers of previous interrupted update should be removed.
    WriteFile(cWorkingDir / "image.squashfs.tmp", "partial image");

    for (const auto& testCase : cTestCases) {
        LOG_INF() << "Test case" << Log::Field("name", testCase.mName.c_str());

        auto status = std::make_unique<InstanceStatus>();

        ExpectUpdateImage("rootfsImageDigest", testCase.mLayerDigest, testCase.mMediaType, testCase.mSize);

        if (testCase.mExpectBlob) {
            ExpectLayerBlob(testCase.mLayerDigest);
        }

        auto err = mRootfsRuntime.StartInstance(*CreateInstance("itemId", "rootfsImageDigest"), *status);
        EXPECT_TRUE(err.Is(testCase.mError)) << tests::utils::ErrorToStr(err);

        EXPECT_EQ(status->mState, InstanceStateEnum::eFailed);
        EXPECT_TRUE(GetWorkingDirFiles().empty());
        EXPECT_EQ(mStorage.GetInstances(), std::vector {CreateCurrent()});
        EXPECT_FALSE(IsRebootRequested());
    }

    Stop();
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
    EXPECT_TRUE(GetWorkingDirFiles().empty());

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

    // Copied image info can't be calculated.

    status = std::make_unique<InstanceStatus>();

    mFileInfoProvider.mFail = true;

    ExpectValidUpdate("rootfsImageDigest");

    err = mRootfsRuntime.StartInstance(*CreateInstance("itemId", "rootfsImageDigest"), *status);
    EXPECT_TRUE(err.Is(ErrorEnum::eFailed)) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(status->mState, InstanceStateEnum::eFailed);
    EXPECT_TRUE(GetWorkingDirFiles().empty());
    EXPECT_EQ(mStorage.GetInstances(), std::vector {CreateCurrent()});

    Stop();
}

TEST_F(RootfsRuntimeTest, StartInstanceReplacesPendingUpdate)
{
    // Failed pending instance is replaced by the new update.

    mStorage.SetInstances({CreateCurrent(), CreatePending(InstanceStateEnum::eFailed, Error(ErrorEnum::eFailed, "x"))});

    InitAndStart();

    GetStatuses();

    auto status = std::make_unique<InstanceStatus>();

    ExpectValidUpdate("firstManifest", "vnd.aos.image.component.inc.v1");

    auto err = mRootfsRuntime.StartInstance(*CreateInstance("itemId", "firstManifest"), *status);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
    EXPECT_EQ(ReadStateValue("type"), "incremental");
    EXPECT_FALSE(mStorage.GetInstance("updateManifestDigest").has_value());

    const auto cSecondFile = cTestDir / "rootfs.1.0.2";

    WriteFile(cSecondFile, "rootfs image 2");

    const auto cSecondDigest = CalculateSHA256(cSecondFile);

    ExpectUpdateImage(
        "secondManifest", cSecondDigest, "vnd.aos.image.component.full.v1", std::filesystem::file_size(cSecondFile));
    ExpectLayerBlob(cSecondDigest, cSecondFile);

    err = mRootfsRuntime.StartInstance(*CreateInstance("itemId", "secondManifest", "1.0.2"), *status);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(GetWorkingDirFiles(), (std::set {cStateFile, cImageFile}));
    EXPECT_EQ(ReadStateValue("type"), "full");
    EXPECT_EQ(CalculateSHA256(cImageFile), cSecondDigest);

    const auto stored = mStorage.GetInstances();

    ASSERT_EQ(stored.size(), 2u);
    EXPECT_EQ(stored[0], CreateCurrent());
    EXPECT_STREQ(stored[1].mManifestDigest.CStr(), "secondManifest");

    Stop();
}

TEST_F(RootfsRuntimeTest, StartInstanceRejectsInstalledVersion)
{
    // Different image with the installed version can't be distinguished from the installed one.

    mStorage.SetInstances({CreateCurrent(), CreatePending()});
    WriteState("prepared");

    InitAndStart();

    GetStatuses();

    auto status = std::make_unique<InstanceStatus>();

    auto err = mRootfsRuntime.StartInstance(*CreateInstance("itemId", "otherManifest", "1.0.0"), *status);
    EXPECT_TRUE(err.Is(ErrorEnum::eInvalidArgument)) << tests::utils::ErrorToStr(err);
    EXPECT_EQ(status->mState, InstanceStateEnum::eFailed);

    // Pending update is kept.
    EXPECT_EQ(ReadStateValue("state"), "prepared");
    EXPECT_EQ(mStorage.GetInstances(), (std::vector {CreateCurrent(), CreatePending()}));

    Stop();
}

TEST_F(RootfsRuntimeTest, StartInstanceAcceptsInstalledVersionOfDefaultCurrent)
{
    // The default current instance has no manifest digest: the instance with the installed version is the installed
    // rootfs, the launcher treats eAlreadyExist as active instance.

    mStorage.SetInstances({});

    InitAndStart();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);

    auto status   = std::make_unique<InstanceStatus>();
    auto instance = std::make_unique<InstanceInfo>();

    static_cast<InstanceIdent&>(*instance) = static_cast<const InstanceIdent&>(statuses[0]);
    instance->mManifestDigest              = "cloudManifest";
    instance->mVersion                     = "1.0.0";

    auto err = mRootfsRuntime.StartInstance(*instance, *status);
    EXPECT_TRUE(err.Is(ErrorEnum::eAlreadyExist)) << tests::utils::ErrorToStr(err);

    EXPECT_TRUE(GetWorkingDirFiles().empty());
    EXPECT_EQ(mStorage.GetInstances().size(), 1u);

    Stop();
}

TEST_F(RootfsRuntimeTest, StartInstanceFailsOnCleanupError)
{
    // Previous state file can't be removed: new update artifacts should not be written and the image should be kept,
    // otherwise initramfs finds the state without image.

    InitAndStart();

    GetStatuses();

    std::filesystem::create_directories(cStateFile / "subdir");
    WriteFile(cImageFile, "image");

    auto status = std::make_unique<InstanceStatus>();

    ExpectUpdateImage("rootfsImageDigest", CalculateSHA256(cUpdateRootfsFile));

    auto err = mRootfsRuntime.StartInstance(*CreateInstance("itemId", "rootfsImageDigest"), *status);
    EXPECT_FALSE(err.IsNone());

    EXPECT_EQ(status->mState, InstanceStateEnum::eFailed);
    EXPECT_EQ(GetWorkingDirFiles(), (std::set {cStateFile, cImageFile}));
    EXPECT_EQ(ReadFile(cImageFile), "image");
    EXPECT_FALSE(IsRebootRequested());

    Stop();
}

TEST_F(RootfsRuntimeTest, StartInstanceFailsOnStorageError)
{
    // Pending instance can't be stored: the state file is not created and the image is removed.

    InitAndStart();

    GetStatuses();

    ExpectValidUpdate("newManifest");

    mStorage.mFailWrite = true;

    auto status = std::make_unique<InstanceStatus>();

    auto err = mRootfsRuntime.StartInstance(*CreateInstance("itemId", "newManifest"), *status);
    EXPECT_FALSE(err.IsNone());

    EXPECT_EQ(status->mState, InstanceStateEnum::eFailed);
    EXPECT_TRUE(GetWorkingDirFiles().empty());
    EXPECT_EQ(mStorage.GetInstances(), std::vector {CreateCurrent()});
    EXPECT_FALSE(IsRebootRequested());

    Stop();
}

TEST_F(RootfsRuntimeTest, StartInstanceKeepsUpdateOnRebootRequestError)
{
    InitAndStart();

    GetStatuses();

    mStatusReceiver.mFailReboot = true;

    auto status = std::make_unique<InstanceStatus>();

    ExpectValidUpdate("newManifest");

    auto err = mRootfsRuntime.StartInstance(*CreateInstance("itemId", "newManifest"), *status);
    EXPECT_TRUE(err.Is(ErrorEnum::eFailed)) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(status->mState, InstanceStateEnum::eActivating);
    EXPECT_TRUE(status->mError.Is(ErrorEnum::eFailed));
    EXPECT_EQ(GetWorkingDirFiles(), (std::set {cStateFile, cImageFile}));

    // Prepared update is kept, reboot request is retried without preparing the update again.

    mStatusReceiver.mFailReboot = false;

    status = std::make_unique<InstanceStatus>();

    err = mRootfsRuntime.StartInstance(*CreateInstance("itemId", "newManifest"), *status);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(status->mState, InstanceStateEnum::eActivating);
    EXPECT_TRUE(IsRebootRequested());

    Stop();
}

TEST_F(RootfsRuntimeTest, StartInstanceIsDeferredInTrialBoot)
{
    // The update in trial boot is finalized first: the new update doesn't wait for the health check and doesn't change
    // the trial update. It is prepared when the launcher starts the instance again on next start.

    mStorage.SetInstances({CreateCurrent(), CreatePending()});
    WriteState("trial");
    WriteFile(cVersionFile, R"(VERSION="1.0.1")");

    std::promise<void> checkPromise;
    auto               checkFuture = checkPromise.get_future().share();

    EXPECT_CALL(mSystemdConn, GetUnitStatus("sm")).WillOnce(Invoke([checkFuture](const auto&) {
        checkFuture.wait();

        sm::utils::UnitStatus status;

        status.mName        = "sm";
        status.mActiveState = sm::utils::UnitStateEnum::eActive;

        return RetWithError<sm::utils::UnitStatus>(status, ErrorEnum::eNone);
    }));
    EXPECT_CALL(mItemInfoProvider, GetBlobPath).Times(0);

    InitAndStart();

    auto status = std::make_unique<InstanceStatus>();

    auto err = mRootfsRuntime.StartInstance(*CreateInstance("itemId", "newManifest", "1.0.2"), *status);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(status->mState, InstanceStateEnum::eActivating);
    EXPECT_STREQ(status->mManifestDigest.CStr(), "newManifest");
    EXPECT_EQ(GetWorkingDirFiles(), (std::set {cStateFile, cImageFile}));
    EXPECT_EQ(ReadStateValue("state"), "trial");
    EXPECT_FALSE(mStorage.GetInstance("newManifest").has_value());

    checkPromise.set_value();

    Stop();

    // The verdict of the trial update is stored.
    EXPECT_EQ(ReadStateValue("state"), "confirmed");
    EXPECT_TRUE(mStorage.GetInstance("updateManifestDigest").has_value());
    EXPECT_FALSE(mStorage.GetInstance("newManifest").has_value());
}

TEST_F(RootfsRuntimeTest, StartInstanceIsDeferredUntilReboot)
{
    // SM is restarted in trial boot after the verdict: the new update is deferred, the verdict and the reboot request
    // are kept. After reboot the previous update is finished and the new update is prepared.

    for (const auto& state : {"confirmed", "rejected"}) {
        LOG_INF() << "Test case" << Log::Field("state", state);

        mStorage.SetInstances({CreateCurrent(), CreatePending()});
        WriteState(state);

        EXPECT_CALL(mItemInfoProvider, GetBlobPath).Times(0);

        InitAndStart();

        GetStatuses();

        EXPECT_TRUE(IsRebootRequested());

        auto status = std::make_unique<InstanceStatus>();

        auto err = mRootfsRuntime.StartInstance(*CreateInstance("itemId", "newManifest", "1.0.2"), *status);
        ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

        EXPECT_EQ(status->mState, InstanceStateEnum::eActivating);
        EXPECT_TRUE(IsRebootRequested());
        EXPECT_EQ(GetWorkingDirFiles(), (std::set {cStateFile, cImageFile}));
        EXPECT_EQ(ReadStateValue("state"), state);
        EXPECT_FALSE(mStorage.GetInstance("newManifest").has_value());

        Stop();

        // Initramfs reverts the update on reboot.

        WriteState("reverted");

        err = mRootfsRuntime.Start();
        ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

        GetStatuses();

        ExpectValidUpdate("newManifest");

        err = mRootfsRuntime.StartInstance(*CreateInstance("itemId", "newManifest", "1.0.2"), *status);
        ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

        EXPECT_EQ(status->mState, InstanceStateEnum::eActivating);
        EXPECT_TRUE(IsRebootRequested());
        EXPECT_EQ(ReadStateValue("state"), "prepared");
        EXPECT_FALSE(mStorage.GetInstance("updateManifestDigest").has_value());
        EXPECT_TRUE(mStorage.GetInstance("newManifest").has_value());

        Stop();
    }
}

TEST_F(RootfsRuntimeTest, StartInstanceDropsPreviousUpdateOnSyncError)
{
    // The previous state is removed, but the directory sync fails: the previous update is dropped, not kept.

    mStorage.SetInstances({CreateCurrent(), CreatePending()});
    WriteState("prepared");

    InitAndStart();

    GetStatuses();

    // Reboot request for the previous update is sent on start.
    EXPECT_TRUE(IsRebootRequested());

    const auto cDir   = std::filesystem::canonical(cWorkingDir);
    auto       failed = std::make_shared<std::atomic_bool>(false);

    SetFaults(
        nullptr, [cDir, failed](const std::filesystem::path& path) { return path == cDir && !failed->exchange(true); });

    ExpectUpdateImage("newManifest", CalculateSHA256(cUpdateRootfsFile));

    auto status = std::make_unique<InstanceStatus>();

    auto err = mRootfsRuntime.StartInstance(*CreateInstance("itemId", "newManifest", "1.0.2"), *status);
    EXPECT_FALSE(err.IsNone());

    EXPECT_EQ(status->mState, InstanceStateEnum::eFailed);
    EXPECT_TRUE(GetWorkingDirFiles().empty());
    EXPECT_EQ(mStorage.GetInstances(), std::vector {CreateCurrent()});
    EXPECT_FALSE(IsRebootRequested());

    Stop();
}

TEST_F(RootfsRuntimeTest, StartInstanceKeepsPendingOnRemoveError)
{
    // Prepared state can't be stored and the new pending instance can't be removed: it is kept and removed by the next
    // update.

    InitAndStart();

    GetStatuses();

    const auto cStateTmpPath = std::filesystem::canonical(cWorkingDir) / "update_state.tmp";

    SetFaults(nullptr, [cStateTmpPath](const std::filesystem::path& path) { return path == cStateTmpPath; });

    ExpectValidUpdate("firstManifest");

    auto status = std::make_unique<InstanceStatus>();

    mStorage.mFailRemove = true;

    auto err = mRootfsRuntime.StartInstance(*CreateInstance("itemId", "firstManifest"), *status);
    EXPECT_FALSE(err.IsNone());

    EXPECT_EQ(status->mState, InstanceStateEnum::eFailed);
    EXPECT_TRUE(GetWorkingDirFiles().empty());
    EXPECT_TRUE(mStorage.GetInstance("firstManifest").has_value());

    SetFaults(nullptr);
    mStorage.mFailRemove = false;

    ExpectValidUpdate("secondManifest");

    err = mRootfsRuntime.StartInstance(*CreateInstance("itemId", "secondManifest", "1.0.2"), *status);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(ReadStateValue("state"), "prepared");
    EXPECT_FALSE(mStorage.GetInstance("firstManifest").has_value());
    EXPECT_TRUE(mStorage.GetInstance("secondManifest").has_value());
    EXPECT_EQ(mStorage.GetInstances().size(), 2u);

    Stop();
}

TEST_F(RootfsRuntimeTest, StartInstanceKeepsPendingIfStateIsNotRemoved)
{
    // Prepared state is renamed, but the directory sync fails, and the state can't be removed on cleanup: the prepared
    // state may survive power loss, so the pending instance is kept and the update stays tracked.

    InitAndStart();

    GetStatuses();

    const auto cDir       = std::filesystem::canonical(cWorkingDir);
    const auto cStatePath = std::filesystem::absolute(cStateFile);

    SetFaults(
        [cStatePath](const std::filesystem::path& path) { return path == cStatePath && std::filesystem::exists(path); },
        [cDir](const std::filesystem::path& path) { return path == cDir && std::filesystem::exists(cStateFile); });

    ExpectValidUpdate("newManifest");

    auto status = std::make_unique<InstanceStatus>();

    auto err = mRootfsRuntime.StartInstance(*CreateInstance("itemId", "newManifest", "1.0.2"), *status);
    EXPECT_FALSE(err.IsNone());

    EXPECT_EQ(status->mState, InstanceStateEnum::eFailed);
    EXPECT_FALSE(IsRebootRequested());
    EXPECT_EQ(ReadStateValue("state"), "prepared");
    EXPECT_TRUE(mStorage.GetInstance("newManifest").has_value());

    GetStatuses();

    Stop();

    // SM restart processes the tracked prepared update.

    SetFaults(nullptr);

    err = mRootfsRuntime.Start();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eActivating);
    EXPECT_STREQ(statuses[0].mManifestDigest.CStr(), "newManifest");
    EXPECT_TRUE(IsRebootRequested());
    EXPECT_EQ(ReadStateValue("state"), "prepared");

    Stop();
}

TEST_F(RootfsRuntimeTest, SeveralPendingInstancesAreRemoved)
{
    // Stale pending instance is left by failed cleanup: the tracked one can't be distinguished, the update is
    // untracked.

    mStorage.SetInstances(
        {CreateCurrent(), CreatePending(), CreatePending(InstanceStateEnum::eActivating, {}, "1.0.2")});
    WriteState("prepared");

    InitAndStart();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eActive);
    EXPECT_STREQ(statuses[0].mItemID.CStr(), "itemId");

    EXPECT_TRUE(GetWorkingDirFiles().empty());
    EXPECT_EQ(mStorage.GetInstances(), std::vector {CreateCurrent()});
    EXPECT_FALSE(IsRebootRequested());

    Stop();
}

TEST_F(RootfsRuntimeTest, PendingWithoutStateIsFailed)
{
    // Interrupted after the pending instance is stored but before the state file is created: the update didn't start.

    mStorage.SetInstances({CreateCurrent(), CreatePending()});
    WriteFile(cImageFile, "image");

    InitAndStart();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 2u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eFailed);
    EXPECT_STREQ(statuses[0].mItemID.CStr(), "updateItemId");
    EXPECT_STREQ(statuses[0].mError.Message(), "update is not started");
    EXPECT_EQ(statuses[1].mState, InstanceStateEnum::eActive);
    EXPECT_STREQ(statuses[1].mItemID.CStr(), "itemId");

    EXPECT_TRUE(GetWorkingDirFiles().empty());
    EXPECT_FALSE(mStorage.GetInstance("updateManifestDigest").has_value());
    EXPECT_FALSE(IsRebootRequested());

    Stop();
}

TEST_F(RootfsRuntimeTest, InterruptedUpdateIsPreparedOnStart)
{
    // The update didn't start: the launcher prepares it again on SM start without waiting for a retry.

    mStorage.SetInstances({CreateCurrent(), CreatePending()});

    InitAndStart();

    ASSERT_EQ(GetStatuses().size(), 2u);

    ExpectValidUpdate("updateManifestDigest");

    auto status = std::make_unique<InstanceStatus>();

    auto err = mRootfsRuntime.StartInstance(*CreatePendingInstance(), *status);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(status->mState, InstanceStateEnum::eActivating);
    EXPECT_EQ(ReadStateValue("state"), "prepared");
    EXPECT_TRUE(IsRebootRequested());

    Stop();
}

TEST_F(RootfsRuntimeTest, StoredFailedUpdateIsKept)
{
    // Failed update is reported on every start, and its failure is returned to every start instance request.

    mStorage.SetInstances(
        {CreateCurrent(), CreatePending(InstanceStateEnum::eFailed, Error(ErrorEnum::eFailed, "health check failed"))});

    for (auto i = 0; i < 2; i++) {
        InitAndStart();

        auto statuses = GetStatuses();

        ASSERT_EQ(statuses.size(), 2u);
        EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eFailed);
        EXPECT_STREQ(statuses[0].mError.Message(), "health check failed");
        EXPECT_EQ(statuses[1].mState, InstanceStateEnum::eActive);
        EXPECT_EQ(mStorage.GetInstance("updateManifestDigest")->mState, InstanceStateEnum::eFailed);

        auto status = std::make_unique<InstanceStatus>();

        auto err = mRootfsRuntime.StartInstance(*CreatePendingInstance(), *status);
        EXPECT_FALSE(err.IsNone());

        EXPECT_EQ(status->mState, InstanceStateEnum::eFailed);
        EXPECT_STREQ(status->mError.Message(), "health check failed");
        EXPECT_FALSE(IsRebootRequested());

        GetStatuses();

        Stop();
    }
}

TEST_F(RootfsRuntimeTest, PreparedRequestsReboot)
{
    // SM is restarted after the update is prepared but before reboot.

    mStorage.SetInstances({CreateCurrent(), CreatePending()});
    WriteState("prepared");

    mStatusReceiver.mFailReboot = true;

    InitAndStart();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eActivating);
    EXPECT_STREQ(statuses[0].mItemID.CStr(), "updateItemId");
    EXPECT_TRUE(statuses[0].mError.Is(ErrorEnum::eFailed)) << tests::utils::ErrorToStr(statuses[0].mError);
    EXPECT_FALSE(IsRebootRequested());

    // Reboot request is retried on start instance request.

    mStatusReceiver.mFailReboot = false;

    auto status = std::make_unique<InstanceStatus>();

    auto err = mRootfsRuntime.StartInstance(*CreatePendingInstance(), *status);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(status->mState, InstanceStateEnum::eActivating);
    EXPECT_TRUE(IsRebootRequested());
    EXPECT_EQ(GetWorkingDirFiles(), (std::set {cStateFile, cImageFile}));

    Stop();
}

TEST_F(RootfsRuntimeTest, TrialIsConfirmed)
{
    mStorage.SetInstances({CreateCurrent(), CreatePending()});
    WriteState("trial");
    WriteFile(cVersionFile, R"(VERSION="1.0.1")");

    InitAndStart();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eActivating);
    EXPECT_STREQ(statuses[0].mItemID.CStr(), "updateItemId");
    EXPECT_STREQ(statuses[0].mVersion.CStr(), "1.0.1");

    statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eActivating);
    EXPECT_TRUE(statuses[0].mError.IsNone());
    EXPECT_TRUE(IsRebootRequested());

    Stop();

    EXPECT_EQ(ReadFile(cStateFile), "state=confirmed\ntype=full\nreason=\n");
    EXPECT_EQ(mStorage.GetInstance("updateManifestDigest")->mState, InstanceStateEnum::eActivating);

    // SM restart before reboot should not run health check again.

    EXPECT_CALL(mSystemdConn, GetUnitStatus).Times(0);

    auto err = mRootfsRuntime.Start();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eActivating);
    EXPECT_TRUE(IsRebootRequested());

    Stop();
}

TEST_F(RootfsRuntimeTest, TrialIsRejected)
{
    mStorage.SetInstances({CreateCurrent(), CreatePending()});
    WriteState("trial");
    WriteFile(cVersionFile, R"(VERSION="1.0.1")");

    ExpectUnitState(sm::utils::UnitStateEnum::eFailed);

    InitAndStart();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eActivating);

    statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eFailed);
    EXPECT_FALSE(statuses[0].mError.IsNone());
    EXPECT_TRUE(IsRebootRequested());

    Stop();

    const auto reason = ReadStateValue("reason");

    EXPECT_EQ(ReadStateValue("state"), "rejected");
    EXPECT_FALSE(reason.empty());
    EXPECT_EQ(mStorage.GetInstance("updateManifestDigest")->mState, InstanceStateEnum::eFailed);

    // SM restart before reboot: the rejected update is kept until initramfs reverts it and reboot is requested again.

    auto err = mRootfsRuntime.Start();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eFailed);
    EXPECT_STREQ(statuses[0].mItemID.CStr(), "updateItemId");
    EXPECT_TRUE(IsRebootRequested());
    EXPECT_EQ(GetWorkingDirFiles(), (std::set {cStateFile, cImageFile}));
    EXPECT_EQ(ReadStateValue("state"), "rejected");

    // The rejected update isn't prepared until initramfs reverts it.

    auto status = std::make_unique<InstanceStatus>();

    err = mRootfsRuntime.StartInstance(*CreatePendingInstance(), *status);
    EXPECT_FALSE(err.IsNone());

    EXPECT_EQ(status->mState, InstanceStateEnum::eFailed);
    EXPECT_TRUE(IsRebootRequested());
    EXPECT_EQ(ReadStateValue("state"), "rejected");

    Stop();

    WriteState("reverted", reason);

    err = mRootfsRuntime.Start();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    GetStatuses();

    // The rejected update stays failed and is retried after the instance is stopped.

    err = mRootfsRuntime.StartInstance(*CreatePendingInstance(), *status);
    EXPECT_FALSE(err.IsNone());

    EXPECT_EQ(status->mState, InstanceStateEnum::eFailed);

    err = mRootfsRuntime.StopInstance(*CreatePendingInstance(), *status);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    ExpectValidUpdate("updateManifestDigest");

    err = mRootfsRuntime.StartInstance(*CreatePendingInstance(), *status);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(status->mState, InstanceStateEnum::eActivating);
    EXPECT_EQ(ReadStateValue("state"), "prepared");

    Stop();
}

TEST_F(RootfsRuntimeTest, RejectedWithoutFailedPendingIsCompleted)
{
    // Interrupted after rejected state is stored but before the failure is stored.

    mStorage.SetInstances({CreateCurrent(), CreatePending()});
    WriteState("rejected", "health check failed");

    InitAndStart();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eFailed);
    EXPECT_STREQ(statuses[0].mError.Message(), "health check failed");
    EXPECT_TRUE(IsRebootRequested());

    EXPECT_EQ(mStorage.GetInstance("updateManifestDigest")->mState, InstanceStateEnum::eFailed);

    Stop();
}

TEST_F(RootfsRuntimeTest, TrialVersionMismatchIsRejected)
{
    mStorage.SetInstances({CreateCurrent(), CreatePending()});
    WriteState("trial");

    EXPECT_CALL(mSystemdConn, GetUnitStatus).Times(0);

    InitAndStart();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eFailed);
    EXPECT_STREQ(statuses[0].mItemID.CStr(), "updateItemId");
    EXPECT_STREQ(statuses[0].mError.Message(), "rootfs version mismatch");
    EXPECT_TRUE(IsRebootRequested());

    EXPECT_EQ(ReadStateValue("state"), "rejected");
    EXPECT_EQ(ReadStateValue("reason"), "rootfs version mismatch");
    EXPECT_EQ(mStorage.GetInstance("updateManifestDigest")->mState, InstanceStateEnum::eFailed);

    Stop();
}

TEST_F(RootfsRuntimeTest, VerdictStoreErrorRequestsReboot)
{
    // Neither confirmed nor rejected state can be stored: the update is reported as failed, reboot is safe as trial
    // state is reverted by initramfs.

    mStorage.SetInstances({CreateCurrent(), CreatePending()});
    WriteState("trial");
    WriteFile(cVersionFile, R"(VERSION="1.0.1")");

    const auto cStateTmpPath = std::filesystem::canonical(cWorkingDir) / "update_state.tmp";

    SetFaults(nullptr, [cStateTmpPath](const std::filesystem::path& path) { return path == cStateTmpPath; });

    InitAndStart();

    GetStatuses();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eFailed);
    EXPECT_TRUE(IsRebootRequested());
    EXPECT_EQ(ReadStateValue("state"), "trial");

    Stop();
}

TEST_F(RootfsRuntimeTest, ConfirmStoreErrorRejectsUpdate)
{
    mStorage.SetInstances({CreateCurrent(), CreatePending()});
    WriteState("trial");
    WriteFile(cVersionFile, R"(VERSION="1.0.1")");

    const auto cStateTmpPath = std::filesystem::canonical(cWorkingDir) / "update_state.tmp";
    auto       failed        = std::make_shared<std::atomic_bool>(false);

    SetFaults(nullptr, [cStateTmpPath, failed](const std::filesystem::path& path) {
        return path == cStateTmpPath && !failed->exchange(true);
    });

    InitAndStart();

    GetStatuses();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eFailed);
    EXPECT_TRUE(IsRebootRequested());
    EXPECT_EQ(ReadStateValue("state"), "rejected");

    Stop();
}

TEST_F(RootfsRuntimeTest, HealthCheckRebootRequestErrorIsReported)
{
    mStorage.SetInstances({CreateCurrent(), CreatePending()});
    WriteState("trial");
    WriteFile(cVersionFile, R"(VERSION="1.0.1")");

    mStatusReceiver.mFailReboot = true;

    InitAndStart();

    GetStatuses();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eActivating);
    EXPECT_TRUE(statuses[0].mError.Is(ErrorEnum::eFailed)) << tests::utils::ErrorToStr(statuses[0].mError);

    Stop();

    EXPECT_EQ(ReadStateValue("state"), "confirmed");

    // Reboot request is retried on start instance request.

    mStatusReceiver.mFailReboot = false;

    auto status = std::make_unique<InstanceStatus>();

    auto err = mRootfsRuntime.StartInstance(*CreatePendingInstance(), *status);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_TRUE(IsRebootRequested());
}

TEST_F(RootfsRuntimeTest, StartTwiceWithHealthCheck)
{
    mStorage.SetInstances({CreateCurrent(), CreatePending()});
    WriteState("trial");
    WriteFile(cVersionFile, R"(VERSION="1.0.1")");

    InitAndStart();

    // Second start should join the health check thread of the first one instead of terminating.
    auto err = mRootfsRuntime.Start();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    Stop();
}

TEST_F(RootfsRuntimeTest, ApplyFailedKeepsState)
{
    // Initramfs failed to apply the update: rootfs may be partially updated, the state is kept to retry apply.

    mStorage.SetInstances({CreateCurrent(), CreatePending()});
    WriteState("applying", "rsync failed");

    EXPECT_CALL(mSystemdConn, GetUnitStatus).Times(0);

    InitAndStart();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eFailed);
    EXPECT_STREQ(statuses[0].mItemID.CStr(), "updateItemId");
    EXPECT_STREQ(statuses[0].mError.Message(), "rsync failed");

    EXPECT_FALSE(IsRebootRequested());
    EXPECT_EQ(GetWorkingDirFiles(), (std::set {cStateFile, cImageFile}));
    EXPECT_EQ(ReadStateValue("state"), "applying");
    EXPECT_EQ(mStorage.GetInstance("updateManifestDigest")->mState, InstanceStateEnum::eFailed);

    // Failed apply is not retried: initramfs retries the apply on next boot.

    auto status = std::make_unique<InstanceStatus>();

    auto err = mRootfsRuntime.StartInstance(*CreatePendingInstance(), *status);
    EXPECT_STREQ(err.Message(), "rsync failed");
    EXPECT_EQ(status->mState, InstanceStateEnum::eFailed);
    EXPECT_EQ(ReadStateValue("state"), "applying");

    // The rootfs may be partially updated: the current instance is not reported as active.

    status = std::make_unique<InstanceStatus>();

    err = mRootfsRuntime.StartInstance(*CreateCurrentInstance(), *status);
    EXPECT_STREQ(err.Message(), "rsync failed");
    EXPECT_EQ(status->mState, InstanceStateEnum::eFailed);
    EXPECT_STREQ(status->mItemID.CStr(), "itemId");
    EXPECT_EQ(ReadStateValue("state"), "applying");

    // No update is prepared over the partially updated rootfs: the state and the image are kept for initramfs
    // recovery. The rejected update doesn't clear the unverified rootfs condition, so the next one is rejected as well.

    EXPECT_CALL(mItemInfoProvider, GetBlobPath).Times(0);

    for (const auto& manifest : {"fullManifest", "incrementalManifest"}) {
        status = std::make_unique<InstanceStatus>();

        err = mRootfsRuntime.StartInstance(*CreateInstance("itemId", manifest, "1.0.2"), *status);
        EXPECT_TRUE(err.Is(ErrorEnum::eWrongState)) << tests::utils::ErrorToStr(err);
        EXPECT_EQ(status->mState, InstanceStateEnum::eFailed);
        EXPECT_EQ(GetWorkingDirFiles(), (std::set {cStateFile, cImageFile}));
        EXPECT_EQ(ReadFile(cImageFile), "image");
        EXPECT_EQ(ReadStateValue("state"), "applying");
        EXPECT_EQ(mStorage.GetInstance("updateManifestDigest")->mState, InstanceStateEnum::eFailed);
        EXPECT_FALSE(IsRebootRequested());
    }

    Stop();

    // SM restart keeps the failed apply: the update is still rejected.

    err = mRootfsRuntime.Start();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    GetStatuses();

    status = std::make_unique<InstanceStatus>();

    err = mRootfsRuntime.StartInstance(*CreateInstance("itemId", "fullManifest", "1.0.2"), *status);
    EXPECT_TRUE(err.Is(ErrorEnum::eWrongState)) << tests::utils::ErrorToStr(err);
    EXPECT_EQ(GetWorkingDirFiles(), (std::set {cStateFile, cImageFile}));
    EXPECT_EQ(ReadStateValue("state"), "applying");

    Stop();

    // Initramfs recovers the rootfs on next boot: the new update is prepared.

    WriteState("applied");
    WriteFile(cVersionFile, R"(VERSION="1.0.1")");

    err = mRootfsRuntime.Start();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    GetStatuses();

    ExpectValidUpdate("fullManifest");

    err = mRootfsRuntime.StartInstance(*CreateInstance("itemId", "fullManifest", "1.0.2"), *status);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(ReadStateValue("state"), "prepared");
    EXPECT_TRUE(IsRebootRequested());

    Stop();
}

TEST_F(RootfsRuntimeTest, AppliedIsPromoted)
{
    mStorage.SetInstances({CreateCurrent(), CreatePending()});
    WriteState("applied");
    WriteFile(cVersionFile, R"(VERSION="1.0.1")");

    InitAndStart();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 2u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eInactive);
    EXPECT_STREQ(statuses[0].mItemID.CStr(), "itemId");
    EXPECT_STREQ(statuses[0].mVersion.CStr(), "1.0.0");
    EXPECT_EQ(statuses[1].mState, InstanceStateEnum::eActive);
    EXPECT_STREQ(statuses[1].mItemID.CStr(), "updateItemId");
    EXPECT_STREQ(statuses[1].mSubjectID.CStr(), "updateSubjectId");
    EXPECT_STREQ(statuses[1].mManifestDigest.CStr(), "updateManifestDigest");
    EXPECT_STREQ(statuses[1].mVersion.CStr(), "1.0.1");

    EXPECT_TRUE(GetWorkingDirFiles().empty());
    EXPECT_EQ(mStorage.GetInstances(), std::vector {CreatePending(InstanceStateEnum::eActive)});

    // New current instance is reported on start instance request.

    auto status = std::make_unique<InstanceStatus>();

    auto err = mRootfsRuntime.StartInstance(*CreatePendingInstance(), *status);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);
    EXPECT_EQ(status->mState, InstanceStateEnum::eActive);

    Stop();
}

TEST_F(RootfsRuntimeTest, SameVersionAppliedIsPromoted)
{
    // Applied state is explicit: the update with the same version as the current one is promoted as well.

    mStorage.SetInstances({CreateCurrent(), CreatePending(InstanceStateEnum::eActivating, {}, "1.0.0")});
    WriteState("applied");

    InitAndStart();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 2u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eInactive);
    EXPECT_STREQ(statuses[0].mManifestDigest.CStr(), "manifestDigest");
    EXPECT_EQ(statuses[1].mState, InstanceStateEnum::eActive);
    EXPECT_STREQ(statuses[1].mManifestDigest.CStr(), "updateManifestDigest");

    Stop();
}

TEST_F(RootfsRuntimeTest, InterruptedPromotionIsRepeated)
{
    // Promotion is interrupted after the previous current instance is removed: the promotion is repeated on next start.

    mStorage.SetInstances({CreateCurrent(), CreatePending()});
    WriteState("applied");
    WriteFile(cVersionFile, R"(VERSION="1.0.1")");

    mStorage.mFailUpdate = true;

    auto err = mRootfsRuntime.Init(mConfig, mCurrentNodeInfoProvider, mItemInfoProvider, mOCISpec, mStatusReceiver,
        mSystemdConn, mStorage, mFileInfoProvider);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = mRootfsRuntime.Start();
    EXPECT_FALSE(err.IsNone());

    EXPECT_EQ(mStorage.GetInstances(), std::vector {CreatePending()});
    EXPECT_EQ(ReadStateValue("state"), "applied");

    mStorage.mFailUpdate = false;

    err = mRootfsRuntime.Start();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eActive);
    EXPECT_STREQ(statuses[0].mItemID.CStr(), "updateItemId");

    EXPECT_TRUE(GetWorkingDirFiles().empty());
    EXPECT_EQ(mStorage.GetInstances(), std::vector {CreatePending(InstanceStateEnum::eActive)});

    Stop();
}

TEST_F(RootfsRuntimeTest, MissingCurrentWithoutPromotionIsRestored)
{
    // Current instance is missing, but the applied rootfs doesn't match the pending instance: the default current
    // instance is created and the update is failed.

    mStorage.SetInstances({CreatePending()});
    WriteState("applied");

    InitAndStart();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 2u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eFailed);
    EXPECT_STREQ(statuses[0].mItemID.CStr(), "updateItemId");
    EXPECT_EQ(statuses[1].mState, InstanceStateEnum::eActive);
    EXPECT_STREQ(statuses[1].mItemID.CStr(), "rootfs");
    EXPECT_STREQ(statuses[1].mVersion.CStr(), "1.0.0");

    EXPECT_TRUE(GetWorkingDirFiles().empty());

    Stop();
}

TEST_F(RootfsRuntimeTest, MissingCurrentIsNotRestoredFromTrialRootfs)
{
    // The version file belongs to the trial rootfs: the default current instance is created after the installed rootfs
    // is booted.

    mStorage.SetInstances({CreatePending()});
    WriteState("trial");
    WriteFile(cVersionFile, R"(VERSION="1.0.1")");

    ExpectUnitState(sm::utils::UnitStateEnum::eFailed);

    InitAndStart();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eActivating);
    EXPECT_STREQ(statuses[0].mItemID.CStr(), "updateItemId");

    // Trial is rejected.
    statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eFailed);

    Stop();

    EXPECT_EQ(mStorage.GetInstances().size(), 1u);

    // Initramfs reverts the update and the installed rootfs is booted.

    WriteState("reverted", "health check failed");
    WriteFile(cVersionFile, R"(VERSION="1.0.0")");

    InitAndStart();

    statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 2u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eFailed);
    EXPECT_STREQ(statuses[0].mItemID.CStr(), "updateItemId");
    EXPECT_EQ(statuses[1].mState, InstanceStateEnum::eActive);
    EXPECT_STREQ(statuses[1].mItemID.CStr(), "rootfs");
    EXPECT_STREQ(statuses[1].mVersion.CStr(), "1.0.0");

    Stop();
}

TEST_F(RootfsRuntimeTest, UntrackedAppliedKeepsStateOnStorageError)
{
    // Applied state is kept until the current instance matches the applied rootfs.

    mStorage.SetInstances({CreateCurrent()});
    WriteState("applied");
    WriteFile(cVersionFile, R"(VERSION="1.0.2")");

    mStorage.mFailWrite = true;

    auto err = mRootfsRuntime.Init(mConfig, mCurrentNodeInfoProvider, mItemInfoProvider, mOCISpec, mStatusReceiver,
        mSystemdConn, mStorage, mFileInfoProvider);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = mRootfsRuntime.Start();
    EXPECT_FALSE(err.IsNone());

    EXPECT_EQ(ReadStateValue("state"), "applied");
    EXPECT_EQ(mStorage.GetInstances(), std::vector {CreateCurrent()});

    mStorage.mFailWrite = false;

    err = mRootfsRuntime.Start();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eActive);
    EXPECT_STREQ(statuses[0].mVersion.CStr(), "1.0.2");

    EXPECT_TRUE(GetWorkingDirFiles().empty());

    Stop();
}

TEST_F(RootfsRuntimeTest, AppliedVersionMismatchIsFailed)
{
    struct TestCase {
        std::string mVersion;
        std::string mCurrentItemID;
    };

    const std::vector<TestCase> cTestCases = {
        // The rootfs matches the current instance.
        {"1.0.0", "itemId"},
        // The rootfs matches neither of instances: the current instance is restored from the version file.
        {"1.0.2", "rootfs"},
    };

    for (const auto& testCase : cTestCases) {
        LOG_INF() << "Test case" << Log::Field("version", testCase.mVersion.c_str());

        mStorage.SetInstances({CreateCurrent(), CreatePending()});
        WriteState("applied");
        WriteFile(cVersionFile, "VERSION=\"" + testCase.mVersion + "\"");

        InitAndStart();

        auto statuses = GetStatuses();

        ASSERT_EQ(statuses.size(), 2u);
        EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eFailed);
        EXPECT_STREQ(statuses[0].mError.Message(), "rootfs version mismatch");
        EXPECT_EQ(statuses[1].mState, InstanceStateEnum::eActive);
        EXPECT_STREQ(statuses[1].mItemID.CStr(), testCase.mCurrentItemID.c_str());
        EXPECT_STREQ(statuses[1].mVersion.CStr(), testCase.mVersion.c_str());

        EXPECT_TRUE(GetWorkingDirFiles().empty());
        EXPECT_EQ(mStorage.GetInstance("updateManifestDigest")->mState, InstanceStateEnum::eFailed);
        EXPECT_EQ(mStorage.GetInstances().size(), 2u);

        Stop();
    }
}

TEST_F(RootfsRuntimeTest, AppliedVersionReadErrorKeepsState)
{
    // Version file read error doesn't fail SM start and the applied update: both instances are reported as failed, the
    // state is processed again on next start.

    struct TestCase {
        std::string                                mName;
        std::vector<runtimes::RuntimeInstanceData> mInstances;
        size_t                                     mNumStatuses;
    };

    const std::vector<TestCase> cTestCases = {
        {"tracked", {CreateCurrent(), CreatePending()}, 2},
        // Promotion interrupted after the current instance is removed.
        {"no current", {CreatePending()}, 1},
        {"untracked", {CreateCurrent()}, 1},
    };

    for (const auto& testCase : cTestCases) {
        LOG_INF() << "Test case" << Log::Field("name", testCase.mName.c_str());

        mStorage.SetInstances(testCase.mInstances);
        WriteState("applied");
        std::filesystem::remove(cVersionFile);

        InitAndStart();

        auto statuses = GetStatuses();

        ASSERT_EQ(statuses.size(), testCase.mNumStatuses);

        for (const auto& status : statuses) {
            EXPECT_EQ(status.mState, InstanceStateEnum::eFailed);
            EXPECT_STREQ(status.mError.Message(), "version file not found");
        }

        EXPECT_EQ(ReadStateValue("state"), "applied");
        EXPECT_EQ(mStorage.GetInstances(), testCase.mInstances);
        EXPECT_FALSE(IsRebootRequested());

        // The instances are not reported as active, and the update is not prepared again.

        for (const auto& instance : {CreateCurrentInstance(), CreatePendingInstance()}) {
            if (!mStorage.GetInstance(instance->mManifestDigest.CStr()).has_value()) {
                continue;
            }

            auto status = std::make_unique<InstanceStatus>();

            auto err = mRootfsRuntime.StartInstance(*instance, *status);
            EXPECT_STREQ(err.Message(), "version file not found");
            EXPECT_EQ(status->mState, InstanceStateEnum::eFailed);
        }

        EXPECT_EQ(ReadStateValue("state"), "applied");

        Stop();

        // The update is processed again when the version file is readable.

        WriteFile(cVersionFile, R"(VERSION="1.0.1")");

        auto err = mRootfsRuntime.Start();
        ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

        GetStatuses();

        EXPECT_TRUE(GetWorkingDirFiles().empty());

        Stop();
    }
}

TEST_F(RootfsRuntimeTest, MissingCurrentIsNotRestoredFromConfirmedRootfs)
{
    // Confirmed state on start means that the trial rootfs is running: its version file doesn't belong to the
    // installed rootfs. Reboot is requested to apply the update.

    mStorage.SetInstances({});
    WriteState("confirmed");
    WriteFile(cVersionFile, R"(VERSION="1.0.1")");

    InitAndStart();

    EXPECT_TRUE(mStorage.GetInstances().empty());
    EXPECT_EQ(ReadStateValue("state"), "confirmed");
    EXPECT_TRUE(IsRebootRequested());

    Stop();
}

TEST_F(RootfsRuntimeTest, RevertedAndFailedAreReported)
{
    struct TestCase {
        std::string mState;
        std::string mReason;
        std::string mExpected;
    };

    const std::vector<TestCase> cTestCases = {
        {"reverted", "health check failed", "health check failed"},
        {"reverted", "", "update is reverted"},
        {"failed", "can't mount image", "can't mount image"},
        {"failed", "", "update failed"},
    };

    for (const auto& testCase : cTestCases) {
        LOG_INF() << "Test case" << Log::Field("state", testCase.mState.c_str());

        mStorage.SetInstances({CreateCurrent(), CreatePending()});
        WriteState(testCase.mState, testCase.mReason);

        InitAndStart();

        auto statuses = GetStatuses();

        ASSERT_EQ(statuses.size(), 2u);
        EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eFailed);
        EXPECT_STREQ(statuses[0].mItemID.CStr(), "updateItemId");
        EXPECT_STREQ(statuses[0].mError.Message(), testCase.mExpected.c_str());
        EXPECT_EQ(statuses[1].mState, InstanceStateEnum::eActive);
        EXPECT_STREQ(statuses[1].mItemID.CStr(), "itemId");

        EXPECT_FALSE(IsRebootRequested());
        EXPECT_TRUE(GetWorkingDirFiles().empty());
        EXPECT_EQ(mStorage.GetInstance("updateManifestDigest")->mState, InstanceStateEnum::eFailed);

        Stop();
    }
}

TEST_F(RootfsRuntimeTest, FailedUpdateIsRetriedAfterStop)
{
    mStorage.SetInstances({CreateCurrent(), CreatePending()});
    WriteState("reverted", "health check failed");

    InitAndStart();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 2u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eFailed);
    EXPECT_EQ(mStorage.GetInstance("updateManifestDigest")->mState, InstanceStateEnum::eFailed);

    // The launcher starts stored instances on SM start, and the CM sends scheduled instances on every balancing: they
    // can't be distinguished, so the failed update is not prepared.
    auto status = std::make_unique<InstanceStatus>();

    for (auto i = 0; i < 2; i++) {
        auto err = mRootfsRuntime.StartInstance(*CreatePendingInstance(), *status);
        EXPECT_FALSE(err.IsNone());

        EXPECT_EQ(status->mState, InstanceStateEnum::eFailed);
        EXPECT_STREQ(status->mError.Message(), "health check failed");
        EXPECT_TRUE(GetWorkingDirFiles().empty());
        EXPECT_FALSE(IsRebootRequested());
    }

    // Other instance doesn't clear the failure.
    auto err = mRootfsRuntime.StopInstance(*CreateInstance("itemId", "manifestDigest"), *status);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(mStorage.GetInstance("updateManifestDigest")->mState, InstanceStateEnum::eFailed);

    err = mRootfsRuntime.StopInstance(*CreatePendingInstance(), *status);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(status->mState, InstanceStateEnum::eInactive);
    EXPECT_FALSE(mStorage.GetInstance("updateManifestDigest").has_value());

    ExpectValidUpdate("updateManifestDigest");

    err = mRootfsRuntime.StartInstance(*CreatePendingInstance(), *status);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(status->mState, InstanceStateEnum::eActivating);
    EXPECT_EQ(GetWorkingDirFiles(), (std::set {cStateFile, cImageFile}));
    EXPECT_EQ(ReadStateValue("state"), "prepared");
    EXPECT_EQ(mStorage.GetInstance("updateManifestDigest")->mState, InstanceStateEnum::eActivating);
    EXPECT_EQ(mStorage.GetInstances().size(), 2u);
    EXPECT_TRUE(IsRebootRequested());

    Stop();
}

TEST_F(RootfsRuntimeTest, FailedUpdateIsReplacedByOtherUpdate)
{
    mStorage.SetInstances({CreateCurrent(), CreatePending()});
    WriteState("reverted", "health check failed");

    InitAndStart();

    GetStatuses();

    ExpectValidUpdate("newManifest");

    auto status = std::make_unique<InstanceStatus>();

    auto err = mRootfsRuntime.StartInstance(*CreateInstance("itemId", "newManifest", "1.0.2"), *status);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(status->mState, InstanceStateEnum::eActivating);
    EXPECT_EQ(ReadStateValue("state"), "prepared");
    EXPECT_FALSE(mStorage.GetInstance("updateManifestDigest").has_value());
    EXPECT_TRUE(mStorage.GetInstance("newManifest").has_value());

    Stop();
}

TEST_F(RootfsRuntimeTest, FailureIsKeptIfStopCantRemoveIt)
{
    mStorage.SetInstances({CreateCurrent(), CreatePending(InstanceStateEnum::eFailed)});

    InitAndStart();

    GetStatuses();

    mStorage.mFailRemove = true;

    auto status = std::make_unique<InstanceStatus>();

    auto err = mRootfsRuntime.StopInstance(*CreatePendingInstance(), *status);
    EXPECT_FALSE(err.IsNone());

    EXPECT_EQ(status->mState, InstanceStateEnum::eFailed);
    EXPECT_TRUE(mStorage.GetInstance("updateManifestDigest").has_value());

    err = mRootfsRuntime.StartInstance(*CreatePendingInstance(), *status);
    EXPECT_FALSE(err.IsNone());

    EXPECT_FALSE(IsRebootRequested());

    mStorage.mFailRemove = false;

    err = mRootfsRuntime.StopInstance(*CreatePendingInstance(), *status);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_FALSE(mStorage.GetInstance("updateManifestDigest").has_value());

    Stop();
}

TEST_F(RootfsRuntimeTest, StateIsKeptIfFailureCantBeStored)
{
    // The state is cleared only after the failure is stored: otherwise the failed update looks not started on next
    // start, and the launcher prepares it again.

    mStorage.SetInstances({CreateCurrent(), CreatePending()});
    WriteState("reverted", "health check failed");

    mStorage.mFailUpdate = true;

    InitAndStart();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 2u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eFailed);
    EXPECT_EQ(ReadStateValue("state"), "reverted");

    auto status = std::make_unique<InstanceStatus>();

    auto err = mRootfsRuntime.StartInstance(*CreatePendingInstance(), *status);
    EXPECT_FALSE(err.IsNone());

    EXPECT_FALSE(IsRebootRequested());

    Stop();

    mStorage.mFailUpdate = false;

    InitAndStart();

    GetStatuses();

    EXPECT_FALSE(std::filesystem::exists(cStateFile));
    EXPECT_EQ(mStorage.GetInstance("updateManifestDigest")->mState, InstanceStateEnum::eFailed);

    Stop();
}

TEST_F(RootfsRuntimeTest, InterruptedUpdateIsPreparedAfterRemoveError)
{
    // Not started update can't be removed from storage: it stays tracked, so its record doesn't block the update.

    mStorage.SetInstances({CreateCurrent(), CreatePending()});

    mStorage.mFailRemove = true;

    InitAndStart();

    ASSERT_EQ(GetStatuses().size(), 2u);
    EXPECT_TRUE(mStorage.GetInstance("updateManifestDigest").has_value());

    Stop();

    mStorage.mFailRemove = false;

    InitAndStart();

    ASSERT_EQ(GetStatuses().size(), 2u);

    ExpectValidUpdate("updateManifestDigest");

    auto status = std::make_unique<InstanceStatus>();

    auto err = mRootfsRuntime.StartInstance(*CreatePendingInstance(), *status);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    EXPECT_EQ(status->mState, InstanceStateEnum::eActivating);
    EXPECT_EQ(ReadStateValue("state"), "prepared");
    EXPECT_TRUE(IsRebootRequested());

    Stop();
}

TEST_F(RootfsRuntimeTest, InvalidStateIsCleared)
{
    for (const auto& content : {"state=unknown\n", "garbage", ""}) {
        mStorage.SetInstances({CreateCurrent(), CreatePending()});
        WriteFile(cStateFile, content);
        WriteFile(cImageFile, "image");

        InitAndStart();

        auto statuses = GetStatuses();

        ASSERT_EQ(statuses.size(), 2u);
        EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eFailed);
        EXPECT_STREQ(statuses[0].mError.Message(), "update is not started");
        EXPECT_EQ(statuses[1].mState, InstanceStateEnum::eActive);

        EXPECT_TRUE(GetWorkingDirFiles().empty());

        Stop();
    }
}

TEST_F(RootfsRuntimeTest, UntrackedState)
{
    // Update state without pending instance: storage is lost or the promotion is interrupted after storage update.

    struct TestCase {
        std::string mState;
        std::string mVersion;
        std::string mKeptState;
        bool        mReboot;
        std::string mCurrentItemID;
    };

    const std::vector<TestCase> cTestCases = {
        {"prepared", "1.0.0", "", false, "itemId"},
        // Trial rootfs may be running: the update is rejected and kept until initramfs reverts it.
        {"trial", "1.0.1", "rejected", true, "itemId"},
        {"rejected", "1.0.1", "rejected", true, "itemId"},
        // Trial rootfs is running: reboot is requested to apply the update.
        {"confirmed", "1.0.0", "confirmed", true, "itemId"},
        {"applying", "1.0.0", "applying", false, "itemId"},
        // Promotion interrupted after storage update: current instance matches the applied rootfs.
        {"applied", "1.0.0", "", false, "itemId"},
        // Storage lost: current instance is restored from the applied rootfs.
        {"applied", "1.0.2", "", false, "rootfs"},
        {"reverted", "1.0.0", "", false, "itemId"},
    };

    for (const auto& testCase : cTestCases) {
        LOG_INF() << "Test case" << Log::Field("state", testCase.mState.c_str());

        mStorage.SetInstances({CreateCurrent()});
        WriteState(testCase.mState);
        WriteFile(cVersionFile, "VERSION=\"" + testCase.mVersion + "\"");

        EXPECT_CALL(mSystemdConn, GetUnitStatus).Times(0);

        InitAndStart();

        auto statuses = GetStatuses();

        ASSERT_EQ(statuses.size(), 1u);
        EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eActive);
        EXPECT_STREQ(statuses[0].mItemID.CStr(), testCase.mCurrentItemID.c_str());

        EXPECT_EQ(IsRebootRequested(), testCase.mReboot);
        EXPECT_EQ(std::filesystem::exists(cStateFile), !testCase.mKeptState.empty());
        EXPECT_EQ(ReadStateValue("state"), testCase.mKeptState);

        const auto stored = mStorage.GetInstances();

        ASSERT_EQ(stored.size(), 1u);
        EXPECT_STREQ(stored[0].mItemID.CStr(), testCase.mCurrentItemID.c_str());

        Stop();
    }
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

    Stop();
}

} // namespace aos::sm::launcher
