/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <locale>
#include <map>
#include <sstream>
#include <string_view>

#include <Poco/String.h>
#include <Poco/StringTokenizer.h>

#include <core/common/tools/fs.hpp>
#include <core/common/tools/logger.hpp>

#include <common/utils/utils.hpp>

#include <sm/launcher/runtimes/utils/utils.hpp>

#include "config.hpp"
#include "rootfs.hpp"

namespace aos::sm::launcher {

namespace {

/***********************************************************************************************************************
 * Consts
 **********************************************************************************************************************/

constexpr auto cFilePerm                   = 0600;
constexpr auto cSHA256Algorithm            = "sha256";
constexpr auto cImageExtension             = ".squashfs";
constexpr auto cImageFileName              = "image.squashfs";
constexpr auto cTmpExtension               = ".tmp";
constexpr auto cMediaTypeAppPrefix         = "application/";
constexpr auto cFullMediaTypePrefix        = "vnd.aos.image.component.full";
constexpr auto cIncrementalMediaTypePrefix = "vnd.aos.image.component.inc";
constexpr auto cMediaTypeVersionPrefix     = ".v";
constexpr auto cMediaTypeSuffixSeparator   = '+';
constexpr auto cFullUpdateType             = "full";
constexpr auto cIncrementalUpdateType      = "incremental";

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

// All paths below are built from the working dir and version file path provided by SM config, which is trusted root
// owned configuration, and constant file names. NOSONAR comments mark path injection false positives on these sinks.

std::filesystem::path GetTmpPath(const std::filesystem::path& path)
{
    auto tmpPath = path;

    tmpPath += cTmpExtension;

    return tmpPath;
}

// Keeps the first error: the error is assigned only if no error is stored yet.
void KeepFirstError(Error& firstErr, const Error& err)
{
    if (firstErr.IsNone()) {
        firstErr = err;
    }
}

Error CloseFile(int32_t fd)
{
    if (close(fd) != 0) {
        return Error(errno, "can't close file");
    }

    return ErrorEnum::eNone;
}

Error SyncDir(const std::filesystem::path& dir)
{
    auto fd = open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC); // NOSONAR
    if (fd < 0) {
        return Error(errno, "can't open dir");
    }

    Error err;

    if (fsync(fd) != 0) {
        err = Error(errno, "can't sync dir");
    }

    KeepFirstError(err, CloseFile(fd));

    return err;
}

Error WriteAll(int32_t fd, const char* data, size_t size)
{
    while (size > 0) {
        auto written = write(fd, data, size); // NOSONAR
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }

            return Error(errno, "can't write file");
        }

        data += written;
        size -= written;
    }

    return ErrorEnum::eNone;
}

Error RemoveFile(const std::filesystem::path& path)
{
    if (unlink(path.c_str()) != 0 && errno != ENOENT) { // NOSONAR
        return Error(errno, "can't remove file");
    }

    return ErrorEnum::eNone;
}

Error RemoveFileDurable(const std::filesystem::path& path)
{
    if (auto err = RemoveFile(path); !err.IsNone()) {
        return err;
    }

    return SyncDir(path.parent_path());
}

// Makes file content durable: syncs and closes temporary file, then atomically renames it to the destination path and
// syncs parent directory. On error temporary file is removed.
Error CommitFile(int32_t fd, const std::filesystem::path& tmpPath, const std::filesystem::path& path)
{
    Error err;

    if (fsync(fd) != 0) {
        err = Error(errno, "can't sync file");
    }

    KeepFirstError(err, CloseFile(fd));

    if (err.IsNone() && rename(tmpPath.c_str(), path.c_str()) != 0) { // NOSONAR
        err = Error(errno, "can't rename file");
    }

    if (!err.IsNone()) {
        (void)RemoveFile(tmpPath);

        return err;
    }

    return SyncDir(path.parent_path());
}

Error WriteFileDurable(const std::filesystem::path& path, std::string_view content)
{
    const auto tmpPath = GetTmpPath(path);

    auto fd = open(tmpPath.c_str(), O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, cFilePerm); // NOSONAR
    if (fd < 0) {
        return Error(errno, "can't create file");
    }

    if (auto err = WriteAll(fd, content.data(), content.size()); !err.IsNone()) {
        (void)CloseFile(fd);
        (void)RemoveFile(tmpPath);

        return err;
    }

    return CommitFile(fd, tmpPath, path);
}

// Commits the file written without file descriptor: opens it to sync the content.
Error CommitFile(const std::filesystem::path& tmpPath, const std::filesystem::path& path)
{
    auto fd = open(tmpPath.c_str(), O_RDONLY | O_CLOEXEC); // NOSONAR
    if (fd < 0) {
        Error err(errno, "can't open file");

        (void)RemoveFile(tmpPath);

        return err;
    }

    return CommitFile(fd, tmpPath, path);
}

Error ParseSHA256Digest(const String& digest, Array<uint8_t>& hash)
{
    const std::string digestStr = digest.CStr();
    const auto        pos       = digestStr.find(':');

    if (pos == std::string::npos) {
        return Error(ErrorEnum::eInvalidArgument, "invalid digest format");
    }

    if (Poco::icompare(digestStr.substr(0, pos), cSHA256Algorithm) != 0) {
        return Error(ErrorEnum::eNotSupported, "unsupported digest algorithm");
    }

    if (auto err = String(digestStr.c_str() + pos + 1).HexToByteArray(hash); !err.IsNone()) {
        return Error(ErrorEnum::eInvalidArgument, "invalid digest hash");
    }

    return ErrorEnum::eNone;
}

std::string ErrorToString(const Error& err)
{
    if (err.Message()[0] != '\0') {
        return err.Message();
    }

    return err.StrValue();
}

RetWithError<StaticString<cVersionLen>> GetCurrentVersion(const std::string& versionFilePath)
{
    std::ifstream versionFile(versionFilePath); // NOSONAR
    if (!versionFile.is_open()) {
        return {{}, Error(ErrorEnum::eNotFound, "version file not found")};
    }

    std::string versionFileContent;
    std::getline(versionFile, versionFileContent);

    Poco::StringTokenizer tokenizer(versionFileContent, "=", Poco::StringTokenizer::TOK_TRIM);

    if (tokenizer.count() != 2 || tokenizer[0] != "VERSION") {
        return {{}, Error(ErrorEnum::eInvalidArgument, "invalid version file format")};
    }

    std::string version = tokenizer[1].c_str();
    Poco::removeInPlace(version, '"');

    StaticString<cVersionLen> versionStr;

    if (auto err = versionStr.Assign(version.c_str()); !err.IsNone()) {
        return {{}, AOS_ERROR_WRAP(err)};
    }

    return versionStr;
}

Error VerifyRootfsVersion(const std::string& versionFilePath, const String& expectedVersion)
{
    auto [version, err] = GetCurrentVersion(versionFilePath);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (version != expectedVersion) {
        LOG_ERR() << "Rootfs version mismatch" << Log::Field("current", version)
                  << Log::Field("expected", expectedVersion);

        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "rootfs version mismatch"));
    }

    return ErrorEnum::eNone;
}

using KeyValues = std::map<std::string, std::string, std::less<>>;

// Update state file format: one "<key>=<value>" pair per line, the value is the rest of the line. The format is parsed
// by the initramfs shell script, so values can't contain line breaks.
std::string SanitizeValue(std::string value)
{
    const auto isLineBreak = [](char c) { return c == '\n' || c == '\r'; };

    std::replace_if(value.begin(), value.end(), isLineBreak, ' ');

    return value;
}

RetWithError<KeyValues> ParseKeyValues(const std::filesystem::path& path)
{
    std::ifstream file(path); // NOSONAR
    if (!file.is_open()) {
        return {{}, Error(ErrorEnum::eNotFound, "can't open update state file")};
    }

    KeyValues   values;
    std::string line;

    while (std::getline(file, line)) {
        const auto pos = line.find('=');
        if (pos == std::string::npos) {
            continue;
        }

        // The first value wins: initramfs reads the first matching line.
        (void)values.try_emplace(line.substr(0, pos), line.substr(pos + 1));
    }

    if (file.bad()) {
        return {{}, Error(ErrorEnum::eFailed, "can't read update state file")};
    }

    return values;
}

std::string GetValue(const KeyValues& values, const std::string& key)
{
    if (auto it = values.find(key); it != values.end()) {
        return it->second;
    }

    return "";
}

// Media type format: [application/]<type prefix>.v<version>[+<suffix>].
bool MatchMediaType(std::string_view mediaType, std::string_view typePrefix)
{
    if (mediaType.substr(0, std::string_view(cMediaTypeAppPrefix).size()) == cMediaTypeAppPrefix) {
        mediaType.remove_prefix(std::string_view(cMediaTypeAppPrefix).size());
    }

    if (mediaType.substr(0, typePrefix.size()) != typePrefix) {
        return false;
    }

    mediaType.remove_prefix(typePrefix.size());

    if (mediaType.substr(0, std::string_view(cMediaTypeVersionPrefix).size()) != cMediaTypeVersionPrefix) {
        return false;
    }

    mediaType.remove_prefix(std::string_view(cMediaTypeVersionPrefix).size());

    const auto version = mediaType.substr(0, mediaType.find(cMediaTypeSuffixSeparator));

    return !version.empty()
        && std::all_of(version.begin(), version.end(), [](char c) { return std::isdigit(c, std::locale::classic()); });
}

RetWithError<std::string> GetUpdateType(const oci::ImageManifest& manifest)
{
    const std::string_view mediaType = manifest.mLayers[0].mMediaType.CStr();

    if (MatchMediaType(mediaType, cFullMediaTypePrefix)) {
        return std::string(cFullUpdateType);
    }

    if (MatchMediaType(mediaType, cIncrementalMediaTypePrefix)) {
        return std::string(cIncrementalUpdateType);
    }

    return {"", AOS_ERROR_WRAP(Error(ErrorEnum::eInvalidArgument, "unsupported artifact type"))};
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error RootfsRuntime::Init(const RuntimeConfig& config, iamclient::CurrentNodeInfoProviderItf& currentNodeInfoProvider,
    imagemanager::ItemInfoProviderItf& itemInfoProvider, oci::OCISpecItf& ociSpec,
    InstanceStatusReceiverItf& statusReceiver, sm::utils::SystemdConnItf& systemdConn, runtimes::StorageItf& storage,
    fs::FileInfoProviderItf& fileInfoProvider)
{
    LOG_DBG() << "Init runtime" << Log::Field("type", config.mType.c_str());

    mRuntimeConfig           = config;
    mCurrentNodeInfoProvider = &currentNodeInfoProvider;
    mItemInfoProvider        = &itemInfoProvider;
    mOCISpec                 = &ociSpec;
    mStatusReceiver          = &statusReceiver;
    mStorage                 = &storage;
    mFileInfoProvider        = &fileInfoProvider;

    if (auto err = ParseConfig(mRuntimeConfig, mRootfsConfig); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto er = fs::MakeDirAll(mRootfsConfig.mWorkingDir.c_str()); !er.IsNone()) {
        return AOS_ERROR_WRAP(er);
    }

    if (auto err = CreateRuntimeInfo(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = mUpdateChecker.Init(mRootfsConfig.mHealthCheckServices, systemdConn); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = mRebooter.Init(systemdConn); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

RootfsRuntime::~RootfsRuntime()
{
    // Destroying joinable thread terminates the process: join it if Stop wasn't called.
    JoinHealthCheck();
}

Error RootfsRuntime::Start()
{
    // Health check thread of previous start may still be running.
    JoinHealthCheck();

    // Pending instance (previous current instance on promotion) and current instance.
    auto statuses       = std::make_unique<StaticArray<InstanceStatus, 2>>();
    auto rebootRequired = false;
    auto runHealthCheck = false;

    {
        std::lock_guard lock {mMutex};

        LOG_DBG() << "Start runtime";

        // Update state is loaded first: it defines whether the missing current instance is being promoted.
        if (auto err = LoadUpdateState(); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (auto err = LoadInstances(); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (auto err = ProcessUpdateState(*statuses); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        rebootRequired  = mRebootRequired;
        runHealthCheck  = mRunHealthCheck;
        mRunHealthCheck = false;
    }

    // Status receiver is called outside of runtime lock: the launcher calls the runtime under its own lock.
    if (rebootRequired) {
        KeepFirstError(statuses->Back().mError, SendRebootRequest());
    }

    mStatusReceiver->OnInstancesStatusesReceived(*statuses);

    // Health check is started after start statuses are sent, so its result is always received after them.
    if (runHealthCheck) {
        std::lock_guard lock {mHealthCheckMutex};

        (void)mHealthCheckThread.emplace(
            &RootfsRuntime::RunHealthCheck, this, std::make_unique<InstanceStatus>(statuses->Back()));
    }

    return ErrorEnum::eNone;
}

Error RootfsRuntime::Stop()
{
    LOG_DBG() << "Stop runtime";

    JoinHealthCheck();

    return ErrorEnum::eNone;
}

Error RootfsRuntime::GetRuntimeInfo(RuntimeInfo& runtimeInfo) const
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Get runtime info";

    runtimeInfo = mRuntimeInfo;

    return ErrorEnum::eNone;
}

Error RootfsRuntime::InitInstances(const Array<InstanceInfo>& instancesInfo)
{
    (void)instancesInfo;

    return ErrorEnum::eNone;
}

Error RootfsRuntime::StartInstance(const InstanceInfo& instance, InstanceStatus& status)
{
    LOG_DBG() << "Start instance" << Log::Field("ident", static_cast<const InstanceIdent&>(instance))
              << Log::Field("version", instance.mVersion) << Log::Field("manifestDigest", instance.mManifestDigest)
              << Log::Field("type", instance.mType);

    // Status receiver is notified after runtime lock is released: the launcher calls the runtime under its own lock.
    auto notify = DeferRelease(&status, [this](const InstanceStatus* status) {
        (void)mStatusReceiver->OnInstancesStatusesReceived(Array<InstanceStatus> {status, 1});
    });

    auto  found          = false;
    auto  rebootRequired = false;
    Error err;

    {
        std::lock_guard lock {mMutex};

        Tie(found, err) = GetCurrentOrPendingStatus(instance, status);
        rebootRequired  = mRebootRequired;
    }

    if (!found) {
        static_cast<InstanceIdent&>(status) = static_cast<const InstanceIdent&>(instance);
        status.mVersion                     = instance.mVersion;
        status.mManifestDigest              = instance.mManifestDigest;
        status.mRuntimeID                   = mRuntimeInfo.mRuntimeID;
        status.mState                       = InstanceStateEnum::eActivating;
        status.mError                       = ErrorEnum::eNone;

        mStatusReceiver->OnInstancesStatusesReceived(Array<InstanceStatus> {&status, 1});

        // Health check verdict should be stored before update artifacts are replaced by the new update.
        JoinHealthCheck();

        std::lock_guard lock {mMutex};

        Tie(found, err) = GetCurrentOrPendingStatus(instance, status);

        if (!found) {
            err = PrepareUpdate(instance);
            if (!err.IsNone()) {
                status.mState = InstanceStateEnum::eFailed;
                status.mError = err;

                return AOS_ERROR_WRAP(err);
            }

            FillInstanceStatus(mPending, status);
        }

        rebootRequired = mRebootRequired;
    }

    if (!rebootRequired) {
        return err;
    }

    // Prepared update is kept if reboot request fails: the request is retried on next start instance request.
    const auto rebootErr = SendRebootRequest();

    KeepFirstError(status.mError, rebootErr);
    KeepFirstError(err, rebootErr);

    return err;
}

Error RootfsRuntime::StopInstance(const InstanceIdent& instance, InstanceStatus& status)
{
    LOG_DBG() << "Stop instance" << Log::Field("ident", instance);

    static_cast<InstanceIdent&>(status) = instance;
    status.mState                       = InstanceStateEnum::eInactive;
    status.mError                       = ErrorEnum::eNone;

    mStatusReceiver->OnInstancesStatusesReceived(Array<InstanceStatus> {&status, 1});

    return ErrorEnum::eNone;
}

Error RootfsRuntime::Reboot()
{
    LOG_DBG() << "Reboot runtime";

    return mRebooter.Reboot();
}

Error RootfsRuntime::GetInstanceMonitoringData(
    const InstanceIdent& instanceIdent, monitoring::InstanceMonitoringData& monitoringData)
{
    (void)monitoringData;

    LOG_DBG() << "Get instance monitoring data" << Log::Field("instance", instanceIdent);

    return ErrorEnum::eNotSupported;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

// The verdict is a single state change. If it can't be stored, the update is reported as failed and the reboot is
// requested anyway: the state is either trial, which initramfs reverts, or confirmed, which initramfs applies and SM
// promotes on next start.
void RootfsRuntime::RunHealthCheck(std::unique_ptr<InstanceStatus> status)
{
    LOG_DBG() << "Start health check for rootfs update";

    auto checkErr = mUpdateChecker.Check();
    if (!checkErr.IsNone()) {
        LOG_ERR() << "Rootfs update health check failed" << Log::Field(checkErr);

        checkErr = AOS_ERROR_WRAP(checkErr);
    }

    {
        std::lock_guard lock {mMutex};

        if (mPending.mItemID.IsEmpty() || !mHasUpdateState || mUpdateState != UpdateStateEnum::eTrial
            || static_cast<const InstanceIdent&>(mPending) != static_cast<const InstanceIdent&>(*status)
            || mPending.mManifestDigest != status->mManifestDigest) {
            LOG_WRN() << "Pending rootfs update is changed, health check verdict is discarded";

            return;
        }

        if (checkErr.IsNone()) {
            if (auto err = StoreUpdateState(UpdateStateEnum::eConfirmed); !err.IsNone()) {
                LOG_ERR() << "Can't confirm rootfs update" << Log::Field(err);

                checkErr = err;
            }
        }

        if (!checkErr.IsNone()) {
            if (auto err = StoreUpdateState(UpdateStateEnum::eRejected, ErrorToString(checkErr)); !err.IsNone()) {
                LOG_ERR() << "Can't reject rootfs update" << Log::Field(err);
            }

            SetPendingFailed(checkErr);

            status->mState = mPending.mState;
            status->mError = mPending.mError;
        }

        // Reboot request is retried on start instance request if it fails.
        mRebootRequired = true;
    }

    // Status receiver is called outside of runtime lock: the launcher calls the runtime under its own lock.
    KeepFirstError(status->mError, SendRebootRequest());

    mStatusReceiver->OnInstancesStatusesReceived(Array<InstanceStatus> {status.get(), 1});
}

void RootfsRuntime::JoinHealthCheck()
{
    std::lock_guard lock {mHealthCheckMutex};

    if (mHealthCheckThread.has_value() && mHealthCheckThread->joinable()) {
        mHealthCheckThread->join();
    }

    mHealthCheckThread.reset();
}

Error RootfsRuntime::CreateRuntimeInfo()
{
    auto nodeInfo = std::make_unique<NodeInfo>();

    if (auto err = mCurrentNodeInfoProvider->GetCurrentNodeInfo(*nodeInfo); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = utils::CreateRuntimeInfo(mRuntimeConfig.mType, *nodeInfo, cMaxNumInstances, mRuntimeInfo);
        !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    mDefaultInstanceIdent.mType         = UpdateItemTypeEnum::eComponent;
    mDefaultInstanceIdent.mInstance     = 0;
    mDefaultInstanceIdent.mItemID       = mRuntimeInfo.mRuntimeType;
    mDefaultInstanceIdent.mSubjectID    = nodeInfo->mNodeType;
    mDefaultInstanceIdent.mPreinstalled = true;

    LOG_INF() << "Runtime info" << Log::Field("runtimeID", mRuntimeInfo.mRuntimeID)
              << Log::Field("runtimeType", mRuntimeInfo.mRuntimeType)
              << Log::Field("maxInstances", mRuntimeInfo.mMaxInstances);

    return ErrorEnum::eNone;
}

// Storage keeps the current (active) instance and the pending one. If the current instance doesn't exist (first start),
// it is created from the version file with the default ident. The current instance doesn't exist as well if the
// promotion is interrupted after the previous current instance is removed: the promotion is repeated on applied state
// processing.
Error RootfsRuntime::LoadInstances()
{
    mCurrent = {};

    ResetPending();

    auto instances = std::make_unique<StaticArray<runtimes::RuntimeInstanceData, cMaxNumStoredInstances>>();

    if (auto err = mStorage->GetRuntimeInstances(mRuntimeInfo.mRuntimeID, *instances); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    for (const auto& instance : *instances) {
        if (instance.mState == InstanceStateEnum::eActive) {
            mCurrent = instance;
        } else {
            mPending = instance;
        }
    }

    if (!mCurrent.mItemID.IsEmpty()) {
        return ErrorEnum::eNone;
    }

    if (!mPending.mItemID.IsEmpty() && mHasUpdateState && mUpdateState == UpdateStateEnum::eApplied
        && VerifyRootfsVersion(mRootfsConfig.mVersionFilePath, mPending.mVersion).IsNone()) {
        LOG_WRN() << "Rootfs update promotion is interrupted, repeat promotion";

        return ErrorEnum::eNone;
    }

    LOG_WRN() << "Current rootfs instance is not found, create default";

    return RestoreDefaultCurrent();
}

// State file is written atomically, so it may be invalid only if the filesystem is damaged: invalid state is treated as
// no update.
Error RootfsRuntime::LoadUpdateState()
{
    mHasUpdateState = false;
    mUpdateState    = UpdateStateEnum::eNumStates;
    mUpdateType.clear();
    mUpdateError.clear();

    const auto      path = GetPath(cUpdateStateFileName);
    std::error_code ec;

    // Lookup error should not be treated as missing file, otherwise the update being applied is canceled.
    const auto exists = std::filesystem::exists(path, ec);
    if (ec) {
        return AOS_ERROR_WRAP(Error(ec.value(), "can't check update state file"));
    }

    if (!exists) {
        return ErrorEnum::eNone;
    }

    auto [values, err] = ParseKeyValues(path);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    // Empty value matches the sentinel string of the enum.
    err = mUpdateState.FromString(GetValue(values, "state").c_str());
    if (!err.IsNone() || mUpdateState == UpdateStateEnum::eNumStates) {
        LOG_ERR() << "Rootfs update state is invalid" << Log::Field("state", GetValue(values, "state").c_str());

        return ErrorEnum::eNone;
    }

    mUpdateType     = GetValue(values, "type");
    mUpdateError    = Poco::trim(GetValue(values, "reason"));
    mHasUpdateState = true;

    return ErrorEnum::eNone;
}

// Initramfs always changes prepared, confirmed and rejected states on boot: the state means that SM was restarted
// before reboot. Confirmed state with the reason means that initramfs failed to apply the update.
Error RootfsRuntime::ProcessUpdateState(Array<InstanceStatus>& statuses)
{
    if (!mHasUpdateState) {
        return ProcessNoUpdate(statuses);
    }

    if (mPending.mItemID.IsEmpty()) {
        return ProcessUntracked(statuses);
    }

    LOG_DBG() << "Process rootfs update" << Log::Field("state", mUpdateState)
              << Log::Field("reason", mUpdateError.c_str());

    switch (mUpdateState.GetValue()) {
    case UpdateStateEnum::ePrepared:
        return WaitReboot(statuses);

    case UpdateStateEnum::eTrial:
        return ProcessTrial(statuses);

    case UpdateStateEnum::eConfirmed:
        if (mUpdateError.empty()) {
            return WaitReboot(statuses);
        }

        return ProcessApplyFailed(statuses);

    case UpdateStateEnum::eRejected:
        // SM may be interrupted after rejected state is stored and before the pending instance is failed.
        if (mPending.mState != InstanceStateEnum::eFailed) {
            SetPendingFailed(GetUpdateError("update is rejected"));
        }

        return WaitReboot(statuses);

    case UpdateStateEnum::eApplied:
        return ProcessApplied(statuses);

    case UpdateStateEnum::eReverted:
        return ProcessFailed(statuses, GetUpdateError("update is reverted"));

    default:
        return ProcessFailed(statuses, GetUpdateError("update failed"));
    }
}

// No update state: the pending instance is either failed or the update was interrupted before it is committed.
Error RootfsRuntime::ProcessNoUpdate(Array<InstanceStatus>& statuses)
{
    // Removes invalid state file, stale images and temporary files.
    if (auto err = ClearUpdateArtifacts(); !err.IsNone()) {
        LOG_ERR() << "Failed to clear update artifacts" << Log::Field(err);
    }

    if (!mPending.mItemID.IsEmpty()) {
        if (mPending.mState != InstanceStateEnum::eFailed) {
            SetPendingFailed(AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "update is not started")));
        }

        if (auto err = AddStatus(statuses, mPending); !err.IsNone()) {
            return err;
        }
    }

    return AddStatus(statuses, mCurrent);
}

// Update state without pending instance: storage is lost or the promotion is interrupted after storage is updated.
Error RootfsRuntime::ProcessUntracked(Array<InstanceStatus>& statuses)
{
    LOG_WRN() << "Rootfs update without pending instance" << Log::Field("state", mUpdateState);

    if (mUpdateState == UpdateStateEnum::eConfirmed) {
        // The rootfs may be partially updated, cancellation is not safe: initramfs applies the update, and the current
        // instance is restored from the applied rootfs.
        return AddStatus(statuses, mCurrent);
    }

    // Trial rootfs may be running, but the installed rootfs is not modified: without the state file the installed
    // rootfs is booted.
    if (mUpdateState == UpdateStateEnum::eTrial || mUpdateState == UpdateStateEnum::eRejected) {
        mRebootRequired = true;
    }

    if (auto err = ClearUpdateArtifacts(); !err.IsNone()) {
        LOG_ERR() << "Failed to clear update artifacts" << Log::Field(err);
    }

    if (mUpdateState == UpdateStateEnum::eApplied) {
        auto [version, err] = GetCurrentVersion(mRootfsConfig.mVersionFilePath);
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (version != mCurrent.mVersion) {
            LOG_WRN() << "Current instance doesn't match applied rootfs, restore default"
                      << Log::Field("version", version);

            err = RestoreDefaultCurrent();
            if (!err.IsNone()) {
                return AOS_ERROR_WRAP(err);
            }
        }
    }

    return AddStatus(statuses, mCurrent);
}

// Initramfs mounted the update image as rootfs. SM restart before the verdict is stored runs the health check again.
Error RootfsRuntime::ProcessTrial(Array<InstanceStatus>& statuses)
{
    if (auto err = VerifyRootfsVersion(mRootfsConfig.mVersionFilePath, mPending.mVersion); !err.IsNone()) {
        LOG_ERR() << "Rootfs trial boot verification failed" << Log::Field(err);

        return RejectTrialBoot(statuses, err);
    }

    if (auto err = AddStatus(statuses, mPending); !err.IsNone()) {
        return err;
    }

    // Health check is started by Start after statuses are sent.
    mRunHealthCheck = true;

    return ErrorEnum::eNone;
}

// The rootfs may be partially updated: the state is kept, so initramfs retries the apply on next boot. Reboot is not
// requested to avoid reboot loop.
Error RootfsRuntime::ProcessApplyFailed(Array<InstanceStatus>& statuses)
{
    LOG_ERR() << "Rootfs update apply failed, apply will be retried on next boot"
              << Log::Field("reason", mUpdateError.c_str());

    SetPendingFailed(GetUpdateError(""));

    return AddStatus(statuses, mPending);
}

Error RootfsRuntime::ProcessApplied(Array<InstanceStatus>& statuses)
{
    if (auto err = VerifyRootfsVersion(mRootfsConfig.mVersionFilePath, mPending.mVersion); !err.IsNone()) {
        return ProcessFailed(statuses, err);
    }

    LOG_INF() << "Rootfs update applied" << Log::Field("ident", static_cast<const InstanceIdent&>(mPending))
              << Log::Field("version", mPending.mVersion);

    auto prevInstance = mCurrent;

    if (auto err = PromotePending(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    // If cleanup is interrupted, the state is processed as untracked on next start.
    if (auto err = ClearUpdateArtifacts(); !err.IsNone()) {
        LOG_ERR() << "Failed to clear update artifacts" << Log::Field(err);
    }

    // Previous current instance doesn't exist if the previous promotion is interrupted.
    if (!prevInstance.mItemID.IsEmpty()) {
        prevInstance.mState = InstanceStateEnum::eInactive;

        if (auto err = AddStatus(statuses, prevInstance); !err.IsNone()) {
            return err;
        }
    }

    return AddStatus(statuses, mCurrent);
}

// Update is finished without changing the rootfs: the installed rootfs is running. Failed pending instance is kept in
// storage, so the failure is reported after SM restart.
Error RootfsRuntime::ProcessFailed(Array<InstanceStatus>& statuses, const Error& err)
{
    LOG_ERR() << "Rootfs update failed" << Log::Field("ident", static_cast<const InstanceIdent&>(mPending))
              << Log::Field("version", mPending.mVersion) << Log::Field(err);

    SetPendingFailed(err);

    if (auto clearErr = ClearUpdateArtifacts(); !clearErr.IsNone()) {
        LOG_ERR() << "Failed to clear update artifacts" << Log::Field(clearErr);
    }

    if (auto addErr = AddStatus(statuses, mPending); !addErr.IsNone()) {
        return addErr;
    }

    return AddStatus(statuses, mCurrent);
}

Error RootfsRuntime::WaitReboot(Array<InstanceStatus>& statuses)
{
    LOG_INF() << "Rootfs update is waiting for reboot" << Log::Field("state", mUpdateState);

    mRebootRequired = true;

    return AddStatus(statuses, mPending);
}

// Rejects the update during trial boot: initramfs reverts the update on next boot. Trial state is reverted as well, so
// reboot is requested even if the rejection can't be stored.
Error RootfsRuntime::RejectTrialBoot(Array<InstanceStatus>& statuses, const Error& err)
{
    LOG_ERR() << "Rootfs trial boot is rejected" << Log::Field(err);

    if (auto storeErr = StoreUpdateState(UpdateStateEnum::eRejected, ErrorToString(err)); !storeErr.IsNone()) {
        LOG_ERR() << "Can't reject rootfs update" << Log::Field(storeErr);
    }

    SetPendingFailed(err);

    return WaitReboot(statuses);
}

// Each storage call is durable and atomic. The previous current instance is removed first: if the promotion is
// interrupted, there is no current instance and the promotion is repeated on next start. Making the pending instance
// current first may leave two current instances which can't be distinguished.
Error RootfsRuntime::PromotePending()
{
    if (!mCurrent.mItemID.IsEmpty()) {
        if (auto err = mStorage->RemoveRuntimeInstance(mCurrent.mRuntimeID, mCurrent, mCurrent.mVersion);
            !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        mCurrent = {};
    }

    auto promoted = mPending;

    promoted.mState = InstanceStateEnum::eActive;
    promoted.mError = ErrorEnum::eNone;

    if (auto err = mStorage->UpdateRuntimeInstance(promoted); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    mCurrent = promoted;

    ResetPending();

    return ErrorEnum::eNone;
}

// Replaces the current instance with the default one created from the version file. If it is interrupted after the
// current instance is removed, the default one is created on next start.
Error RootfsRuntime::RestoreDefaultCurrent()
{
    auto [version, err] = GetCurrentVersion(mRootfsConfig.mVersionFilePath);
    if (!err.IsNone()) {
        return err;
    }

    runtimes::RuntimeInstanceData instance;

    static_cast<InstanceIdent&>(instance) = mDefaultInstanceIdent;
    instance.mRuntimeID                   = mRuntimeInfo.mRuntimeID;
    instance.mVersion                     = version;
    instance.mState                       = InstanceStateEnum::eActive;

    if (!mCurrent.mItemID.IsEmpty()) {
        err = mStorage->RemoveRuntimeInstance(mCurrent.mRuntimeID, mCurrent, mCurrent.mVersion);
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        mCurrent = {};
    }

    err = mStorage->AddRuntimeInstance(instance);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    mCurrent = instance;

    return ErrorEnum::eNone;
}

// Storage errors don't stop the update processing: the failure is reported, and the update state is processed again on
// next start.
void RootfsRuntime::SetPendingFailed(const Error& err)
{
    // The error is stored with the same text as the update state reason.
    mPending.mState = InstanceStateEnum::eFailed;
    mPending.mError = err.Message()[0] == '\0' ? Error(err, ErrorToString(err).c_str()) : err;

    if (auto storageErr = mStorage->UpdateRuntimeInstance(mPending); !storageErr.IsNone()) {
        LOG_ERR() << "Can't store failed rootfs update" << Log::Field(AOS_ERROR_WRAP(storageErr));
    }
}

Error RootfsRuntime::AddStatus(Array<InstanceStatus>& statuses, const runtimes::RuntimeInstanceData& instance) const
{
    if (auto err = statuses.EmplaceBack(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    FillInstanceStatus(instance, statuses.Back());

    return ErrorEnum::eNone;
}

// Atomically replaces the update state file. Initramfs changes only the state and the reason.
Error RootfsRuntime::StoreUpdateState(UpdateStateEnum state, const std::string& reason)
{
    std::ostringstream content;

    content << "state=" << UpdateState(state).ToString().CStr() << "\n"
            << "type=" << SanitizeValue(mUpdateType) << "\n"
            << "reason=" << SanitizeValue(reason) << "\n";

    LOG_DBG() << "Store rootfs update state" << Log::Field("state", UpdateState(state));

    if (auto err = WriteFileDurable(GetPath(cUpdateStateFileName), content.str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    mHasUpdateState = true;
    mUpdateState    = state;
    mUpdateError    = reason;

    return ErrorEnum::eNone;
}

Error RootfsRuntime::GetUpdateError(const char* defaultReason) const
{
    return Error(ErrorEnum::eFailed, mUpdateError.empty() ? defaultReason : mUpdateError.c_str());
}

void RootfsRuntime::ResetPending()
{
    mPending        = {};
    mRebootRequired = false;
}

// Fills status if the instance is the current or the pending one. Returns false if the instance is unknown and the
// update should be prepared. Returns the update error if the pending instance is failed: the launcher treats successful
// start as active instance.
RetWithError<bool> RootfsRuntime::GetCurrentOrPendingStatus(const InstanceInfo& instance, InstanceStatus& status) const
{
    for (const auto* known : {&mCurrent, &mPending}) {
        if (known->mItemID.IsEmpty()
            || static_cast<const InstanceIdent&>(instance) != static_cast<const InstanceIdent&>(*known)
            || known->mManifestDigest != instance.mManifestDigest) {
            continue;
        }

        FillInstanceStatus(*known, status);

        if (known->mState != InstanceStateEnum::eFailed) {
            return true;
        }

        return {
            true, known->mError.IsNone() ? AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "update failed")) : known->mError};
    }

    return false;
}

// Should be called outside of runtime lock: the launcher calls the runtime under its own lock.
Error RootfsRuntime::SendRebootRequest()
{
    if (auto err = mStatusReceiver->RebootRequired(mRuntimeInfo.mRuntimeID); !err.IsNone()) {
        LOG_ERR() << "Can't request reboot" << Log::Field(AOS_ERROR_WRAP(err));

        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

void RootfsRuntime::FillInstanceStatus(const runtimes::RuntimeInstanceData& instance, InstanceStatus& status) const
{
    static_cast<InstanceIdent&>(status) = static_cast<const InstanceIdent&>(instance);
    status.mState                       = instance.mState;
    status.mError                       = instance.mError;
    status.mVersion                     = instance.mVersion;
    status.mRuntimeID                   = mRuntimeInfo.mRuntimeID;
    status.mManifestDigest              = instance.mManifestDigest;
    status.mType                        = UpdateItemTypeEnum::eComponent;
}

Error RootfsRuntime::GetImageManifest(const String& digest, oci::ImageManifest& manifest) const
{
    StaticString<cFilePathLen> blobPath;

    if (auto err = mItemInfoProvider->GetBlobPath(digest, blobPath); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = mOCISpec->LoadImageManifest(blobPath, manifest); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (manifest.mLayers.Size() == 0) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eInvalidArgument, "image manifest has no layers"));
    }

    return ErrorEnum::eNone;
}

// Image is copied to temporary file, verified against manifest layer descriptor, synced and atomically renamed.
// Temporary file extension doesn't match initramfs image search pattern, so partially copied image is never used for
// update.
Error RootfsRuntime::CopyImage(const oci::ImageManifest& manifest) const
{
    const auto& layer = manifest.mLayers[0];

    StaticArray<uint8_t, crypto::cSHA256Size> hash;

    if (auto err = ParseSHA256Digest(layer.mDigest, hash); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    StaticString<cFilePathLen> imageArchivePath;

    if (auto err = mItemInfoProvider->GetBlobPath(layer.mDigest, imageArchivePath); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    LOG_DBG() << "Copy image layer" << Log::Field("digest", layer.mDigest)
              << Log::Field("path", imageArchivePath.CStr());

    const auto imagePath = GetPath(cImageFileName);
    const auto tmpPath   = GetTmpPath(imagePath);

    std::error_code ec;

    std::filesystem::copy_file(
        imageArchivePath.CStr(), tmpPath, std::filesystem::copy_options::overwrite_existing, ec); // NOSONAR
    if (ec) {
        (void)RemoveFile(tmpPath);

        return AOS_ERROR_WRAP(Error(ec.value(), "can't copy image"));
    }

    if (auto err = VerifyImage(tmpPath, layer.mSize, hash); !err.IsNone()) {
        (void)RemoveFile(tmpPath);

        return AOS_ERROR_WRAP(err);
    }

    if (auto err = CommitFile(tmpPath, imagePath); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error RootfsRuntime::VerifyImage(const std::filesystem::path& path, size_t size, const Array<uint8_t>& hash) const
{
    auto fileInfo = std::make_unique<fs::FileInfo>();

    if (auto err = mFileInfoProvider->GetFileInfo(path.c_str(), *fileInfo); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (fileInfo->mSize != size) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eInvalidChecksum, "image size mismatch"));
    }

    if (fileInfo->mCheckSum != hash) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eInvalidChecksum, "image digest mismatch"));
    }

    return ErrorEnum::eNone;
}

// The state file is durably removed first and cleanup stops if it fails: the image must never be removed while the
// state file remains. Images and temporary files removal errors don't stop cleanup, the first error is returned.
Error RootfsRuntime::ClearUpdateArtifacts() const
{
    if (auto err = AOS_ERROR_WRAP(RemoveFileDurable(GetPath(cUpdateStateFileName))); !err.IsNone()) {
        LOG_ERR() << "Failed to remove update state" << Log::Field(err);

        return err;
    }

    Error firstErr;

    auto handleErr = [&firstErr](const Error& err, const char* msg) {
        if (err.IsNone()) {
            return;
        }

        LOG_ERR() << msg << Log::Field(err);

        KeepFirstError(firstErr, err);
    };

    std::error_code                     ec;
    std::filesystem::directory_iterator it(mRootfsConfig.mWorkingDir, ec);

    while (!ec && it != std::filesystem::directory_iterator()) {
        if (const auto& path = it->path(); path.extension() == cImageExtension || path.extension() == cTmpExtension) {
            handleErr(AOS_ERROR_WRAP(RemoveFile(path)), "Failed to remove update artifact");
        }

        (void)it.increment(ec);
    }

    if (ec) {
        handleErr(AOS_ERROR_WRAP(Error(ec.value(), "can't iterate working dir")), "Failed to iterate working dir");
    }

    handleErr(AOS_ERROR_WRAP(SyncDir(mRootfsConfig.mWorkingDir)), "Failed to sync working dir");

    return firstErr;
}

// Update artifacts are written in order: image, pending instance in storage, prepared state. The state file is the
// commit point: initramfs starts the update only if it exists, and at this moment the image and the pending instance
// are durable. On error update artifacts and the pending instance are removed.
Error RootfsRuntime::PrepareUpdate(const InstanceInfo& instance)
{
    LOG_DBG() << "Preparing update" << Log::Field("ident", static_cast<const InstanceIdent&>(instance));

    // The state file of the previous update is removed first, so initramfs doesn't act on it anymore.
    if (auto err = ClearUpdateArtifacts(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (!mPending.mItemID.IsEmpty()) {
        if (auto err = mStorage->RemoveRuntimeInstance(mPending.mRuntimeID, mPending, mPending.mVersion);
            !err.IsNone() && !err.Is(ErrorEnum::eNotFound)) {
            return AOS_ERROR_WRAP(err);
        }
    }

    ResetPending();

    auto imageManifest = std::make_unique<oci::ImageManifest>();
    auto pendingAdded  = false;

    auto err = GetImageManifest(instance.mManifestDigest, *imageManifest);

    if (err.IsNone()) {
        Tie(mUpdateType, err) = GetUpdateType(*imageManifest);
    }

    if (err.IsNone()) {
        err = CopyImage(*imageManifest);
    }

    if (err.IsNone()) {
        static_cast<InstanceIdent&>(mPending) = static_cast<const InstanceIdent&>(instance);
        mPending.mRuntimeID                   = mRuntimeInfo.mRuntimeID;
        mPending.mManifestDigest              = instance.mManifestDigest;
        mPending.mVersion                     = instance.mVersion;
        mPending.mState                       = InstanceStateEnum::eActivating;

        err          = mStorage->AddRuntimeInstance(mPending);
        pendingAdded = err.IsNone();
    }

    if (err.IsNone()) {
        err = StoreUpdateState(UpdateStateEnum::ePrepared);
    }

    if (!err.IsNone()) {
        if (auto clearErr = ClearUpdateArtifacts(); !clearErr.IsNone()) {
            LOG_ERR() << "Failed to clear update artifacts" << Log::Field(clearErr);
        }

        if (pendingAdded) {
            if (auto removeErr = mStorage->RemoveRuntimeInstance(mPending.mRuntimeID, mPending, mPending.mVersion);
                !removeErr.IsNone()) {
                LOG_ERR() << "Can't remove pending instance" << Log::Field(AOS_ERROR_WRAP(removeErr));
            }
        }

        ResetPending();

        return AOS_ERROR_WRAP(err);
    }

    mRebootRequired = true;

    return ErrorEnum::eNone;
}

std::filesystem::path RootfsRuntime::GetPath(const std::string& fileName) const
{
    return std::filesystem::path(mRootfsConfig.mWorkingDir) / fileName;
}

} // namespace aos::sm::launcher
