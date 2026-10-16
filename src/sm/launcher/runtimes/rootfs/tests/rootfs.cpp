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

FaultPredicate GetFault(const FaultPredicate& fault)
{
    std::lock_guard lock {sFaultMutex};

    return fault;
}

void SetFaults(FaultPredicate unlinkFault, FaultPredicate fsyncFault = nullptr)
{
    std::lock_guard lock {sFaultMutex};

    sUnlinkFault = std::move(unlinkFault);
    sFsyncFault  = std::move(fsyncFault);
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
            errno = EIO;

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

// In-memory runtime storage with transactions and error injection.
class RuntimeStorageFake : public runtimes::StorageItf {
public:
    Error AddRuntimeInstance(const runtimes::RuntimeInstanceData& instance) override
    {
        std::lock_guard lock {mMutex};

        if (mFailWrite) {
            return ErrorEnum::eFailed;
        }

        if (Find(instance.mRuntimeID, instance, instance.mManifestDigest) != mInstances.end()) {
            return ErrorEnum::eAlreadyExist;
        }

        mInstances.push_back(instance);

        return ErrorEnum::eNone;
    }

    Error UpdateRuntimeInstance(const runtimes::RuntimeInstanceData& instance) override
    {
        std::lock_guard lock {mMutex};

        if (mFailWrite) {
            return ErrorEnum::eFailed;
        }

        auto it = Find(instance.mRuntimeID, instance, instance.mManifestDigest);
        if (it == mInstances.end()) {
            return ErrorEnum::eNotFound;
        }

        *it = instance;

        return ErrorEnum::eNone;
    }

    Error RemoveRuntimeInstance(
        const String& runtimeID, const InstanceIdent& ident, const String& manifestDigest) override
    {
        std::lock_guard lock {mMutex};

        if (mFailWrite) {
            return ErrorEnum::eFailed;
        }

        auto it = Find(runtimeID, ident, manifestDigest);
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

    Error BeginTransaction() override
    {
        std::lock_guard lock {mMutex};

        mSnapshot = mInstances;

        return ErrorEnum::eNone;
    }

    Error CommitTransaction() override
    {
        std::lock_guard lock {mMutex};

        if (mFailCommit) {
            return ErrorEnum::eFailed;
        }

        mSnapshot.reset();

        return ErrorEnum::eNone;
    }

    Error RollbackTransaction() override
    {
        std::lock_guard lock {mMutex};

        if (mSnapshot) {
            mInstances = *mSnapshot;
            mSnapshot.reset();
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
    std::atomic_bool mFailCommit {};

private:
    std::vector<runtimes::RuntimeInstanceData>::iterator Find(
        const String& runtimeID, const InstanceIdent& ident, const String& manifestDigest)
    {
        return std::find_if(mInstances.begin(), mInstances.end(), [&](const auto& instance) {
            return instance.mRuntimeID == runtimeID && static_cast<const InstanceIdent&>(instance) == ident
                && instance.mManifestDigest == manifestDigest;
        });
    }

    mutable std::mutex                                        mMutex;
    std::vector<runtimes::RuntimeInstanceData>                mInstances;
    std::optional<std::vector<runtimes::RuntimeInstanceData>> mSnapshot;
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

    void InitAndStart()
    {
        auto err = mRootfsRuntime.Init(
            mConfig, mCurrentNodeInfoProvider, mItemInfoProvider, mOCISpec, mStatusReceiver, mSystemdConn, mStorage);
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

    auto err = mRootfsRuntime.Init(
        mConfig, mCurrentNodeInfoProvider, mItemInfoProvider, mOCISpec, mStatusReceiver, mSystemdConn, mStorage);
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

    for (const auto& mediaType : cInvalidMediaTypes) {
        LOG_INF() << "Invalid media type" << Log::Field("mediaType", mediaType.c_str());

        auto status = std::make_unique<InstanceStatus>();

        ExpectUpdateImage("invalidManifest", cLayerDigest, mediaType, cLayerSize);

        auto err = mRootfsRuntime.StartInstance(*CreateInstance("itemId", "invalidManifest"), *status);
        EXPECT_TRUE(err.Is(ErrorEnum::eInvalidArgument)) << tests::utils::ErrorToStr(err);

        EXPECT_EQ(status->mState, InstanceStateEnum::eFailed);
        EXPECT_TRUE(GetWorkingDirFiles().empty());
        EXPECT_EQ(mStorage.GetInstances(), std::vector {CreateCurrent()});
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

TEST_F(RootfsRuntimeTest, StartInstanceFailsOnCleanupError)
{
    // Previous state file can't be removed: new update artifacts should not be written and the image should be kept,
    // otherwise initramfs finds the state without image.

    InitAndStart();

    GetStatuses();

    std::filesystem::create_directories(cStateFile / "subdir");
    WriteFile(cImageFile, "image");

    auto status = std::make_unique<InstanceStatus>();

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
    EXPECT_EQ(mStorage.GetInstance("updateManifestDigest")->mState, InstanceStateEnum::eFailed);
    EXPECT_FALSE(IsRebootRequested());

    Stop();
}

TEST_F(RootfsRuntimeTest, FailedUpdateIsReportedAfterRestart)
{
    // Failed pending instance is kept in storage: the failure survives SM restart.

    mStorage.SetInstances(
        {CreateCurrent(), CreatePending(InstanceStateEnum::eFailed, Error(ErrorEnum::eFailed, "health check failed"))});

    InitAndStart();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 2u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eFailed);
    EXPECT_STREQ(statuses[0].mError.Message(), "health check failed");
    EXPECT_EQ(statuses[1].mState, InstanceStateEnum::eActive);

    auto status = std::make_unique<InstanceStatus>();

    auto err = mRootfsRuntime.StartInstance(*CreatePendingInstance(), *status);
    EXPECT_STREQ(err.Message(), "health check failed");
    EXPECT_EQ(status->mState, InstanceStateEnum::eFailed);
    EXPECT_FALSE(IsRebootRequested());

    Stop();
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

    const auto pending = mStorage.GetInstance("updateManifestDigest");

    EXPECT_EQ(pending->mState, InstanceStateEnum::eFailed);
    EXPECT_STREQ(pending->mError.Message(), reason.c_str());

    // SM restart before reboot reports the rejection and requests reboot again.

    auto err = mRootfsRuntime.Start();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eFailed);
    EXPECT_STREQ(statuses[0].mError.Message(), reason.c_str());
    EXPECT_TRUE(IsRebootRequested());

    auto status = std::make_unique<InstanceStatus>();

    err = mRootfsRuntime.StartInstance(*CreatePendingInstance(), *status);
    EXPECT_TRUE(err.Is(ErrorEnum::eFailed)) << tests::utils::ErrorToStr(err);
    EXPECT_EQ(status->mState, InstanceStateEnum::eFailed);

    Stop();
}

TEST_F(RootfsRuntimeTest, RejectedWithoutStoredFailureIsCompleted)
{
    // Interrupted after rejected state is stored but before the pending instance is failed in storage.

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

TEST_F(RootfsRuntimeTest, StartInstanceWaitsForHealthCheck)
{
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

    InitAndStart();

    auto status = std::make_unique<InstanceStatus>();

    ExpectValidUpdate("newManifest");

    auto startFuture = std::async(std::launch::async,
        [&]() { return mRootfsRuntime.StartInstance(*CreateInstance("itemId", "newManifest", "1.0.2"), *status); });

    EXPECT_EQ(startFuture.wait_for(std::chrono::milliseconds(200)), std::future_status::timeout);

    checkPromise.set_value();

    auto err = startFuture.get();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    // New update replaces trial update artifacts and the pending instance.
    EXPECT_EQ(GetWorkingDirFiles(), (std::set {cStateFile, cImageFile}));
    EXPECT_EQ(ReadStateValue("state"), "prepared");
    EXPECT_FALSE(mStorage.GetInstance("updateManifestDigest").has_value());
    EXPECT_TRUE(mStorage.GetInstance("newManifest").has_value());

    Stop();
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
    WriteState("confirmed", "rsync failed");

    EXPECT_CALL(mSystemdConn, GetUnitStatus).Times(0);

    InitAndStart();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eFailed);
    EXPECT_STREQ(statuses[0].mItemID.CStr(), "updateItemId");
    EXPECT_STREQ(statuses[0].mError.Message(), "rsync failed");

    EXPECT_FALSE(IsRebootRequested());
    EXPECT_EQ(GetWorkingDirFiles(), (std::set {cStateFile, cImageFile}));
    EXPECT_EQ(ReadStateValue("state"), "confirmed");
    EXPECT_EQ(mStorage.GetInstance("updateManifestDigest")->mState, InstanceStateEnum::eFailed);

    // New update replaces the failed one.

    ExpectValidUpdate("newManifest");

    auto status = std::make_unique<InstanceStatus>();

    auto err = mRootfsRuntime.StartInstance(*CreateInstance("itemId", "newManifest", "1.0.2"), *status);
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

TEST_F(RootfsRuntimeTest, PromotionStorageErrorIsRetried)
{
    // Promotion transaction fails: storage is rolled back, the state is kept and promotion is repeated on next start.

    mStorage.SetInstances({CreateCurrent(), CreatePending()});
    WriteState("applied");
    WriteFile(cVersionFile, R"(VERSION="1.0.1")");

    mStorage.mFailCommit = true;

    auto err = mRootfsRuntime.Init(
        mConfig, mCurrentNodeInfoProvider, mItemInfoProvider, mOCISpec, mStatusReceiver, mSystemdConn, mStorage);
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    err = mRootfsRuntime.Start();
    EXPECT_FALSE(err.IsNone());

    EXPECT_EQ(mStorage.GetInstances(), (std::vector {CreateCurrent(), CreatePending()}));
    EXPECT_EQ(ReadStateValue("state"), "applied");

    mStorage.mFailCommit = false;

    err = mRootfsRuntime.Start();
    ASSERT_TRUE(err.IsNone()) << tests::utils::ErrorToStr(err);

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 2u);
    EXPECT_EQ(statuses[1].mState, InstanceStateEnum::eActive);
    EXPECT_EQ(mStorage.GetInstances(), std::vector {CreatePending(InstanceStateEnum::eActive)});

    Stop();
}

TEST_F(RootfsRuntimeTest, AppliedVersionMismatchIsFailed)
{
    mStorage.SetInstances({CreateCurrent(), CreatePending()});
    WriteState("applied");

    InitAndStart();

    auto statuses = GetStatuses();

    ASSERT_EQ(statuses.size(), 2u);
    EXPECT_EQ(statuses[0].mState, InstanceStateEnum::eFailed);
    EXPECT_STREQ(statuses[0].mError.Message(), "rootfs version mismatch");
    EXPECT_EQ(statuses[1].mState, InstanceStateEnum::eActive);
    EXPECT_STREQ(statuses[1].mVersion.CStr(), "1.0.0");

    EXPECT_TRUE(GetWorkingDirFiles().empty());

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

        const auto pending = mStorage.GetInstance("updateManifestDigest");

        EXPECT_EQ(pending->mState, InstanceStateEnum::eFailed);
        EXPECT_STREQ(pending->mError.Message(), testCase.mExpected.c_str());

        Stop();
    }
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
        bool        mKeepState;
        bool        mReboot;
        std::string mCurrentItemID;
    };

    const std::vector<TestCase> cTestCases = {
        {"prepared", "1.0.0", false, false, "itemId"},
        {"trial", "1.0.1", false, true, "itemId"},
        {"rejected", "1.0.1", false, true, "itemId"},
        {"confirmed", "1.0.0", true, false, "itemId"},
        // Promotion interrupted after storage update: current instance matches the applied rootfs.
        {"applied", "1.0.0", false, false, "itemId"},
        // Storage lost: current instance is restored from the applied rootfs.
        {"applied", "1.0.2", false, false, "rootfs"},
        {"reverted", "1.0.0", false, false, "itemId"},
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
        EXPECT_EQ(std::filesystem::exists(cStateFile), testCase.mKeepState);

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
