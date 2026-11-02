/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

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

Error RemoveFile(const std::filesystem::path& path)
{
    if (unlink(path.c_str()) != 0 && errno != ENOENT) { // NOSONAR
        return Error(errno, "can't remove file");
    }

    return ErrorEnum::eNone;
}

Error RemoveFileDurable(const fs::FSPlatformItf& fsPlatform, const std::filesystem::path& path)
{
    if (auto err = RemoveFile(path); !err.IsNone()) {
        return err;
    }

    return fsPlatform.Sync(path.parent_path().c_str(), false);
}

// Makes file content durable: syncs temporary file, then atomically renames it to the destination path and syncs
// parent directory. On error temporary file is removed.
Error CommitFile(
    const fs::FSPlatformItf& fsPlatform, const std::filesystem::path& tmpPath, const std::filesystem::path& path)
{
    auto err = fsPlatform.Sync(tmpPath.c_str(), false);

    if (err.IsNone() && rename(tmpPath.c_str(), path.c_str()) != 0) { // NOSONAR
        err = Error(errno, "can't rename file");
    }

    if (!err.IsNone()) {
        (void)RemoveFile(tmpPath);

        return err;
    }

    return fsPlatform.Sync(path.parent_path().c_str(), false);
}

Error WriteFileDurable(
    const fs::FSPlatformItf& fsPlatform, const std::filesystem::path& path, const std::string& content)
{
    const auto tmpPath = GetTmpPath(path);

    if (auto err = fs::WriteStringToFile(tmpPath.c_str(), content.c_str(), cFilePerm); !err.IsNone()) {
        (void)RemoveFile(tmpPath);

        return err;
    }

    return CommitFile(fsPlatform, tmpPath, path);
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
        auto rebootErr = SendRebootRequest();

        if (!statuses->IsEmpty()) {
            KeepFirstError(statuses->Back().mError, rebootErr);
        }
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

    auto  rebootRequired = false;
    Error err;

    {
        std::lock_guard lock {mMutex};

        auto found = false;

        Tie(found, err) = GetCurrentOrPendingStatus(instance, status);

        if (!found) {
            static_cast<InstanceIdent&>(status) = static_cast<const InstanceIdent&>(instance);
            status.mVersion                     = instance.mVersion;
            status.mManifestDigest              = instance.mManifestDigest;
            status.mRuntimeID                   = mRuntimeInfo.mRuntimeID;
            status.mState                       = InstanceStateEnum::eActivating;
            status.mError                       = ErrorEnum::eNone;

            // The update in trial boot is finalized first: the state is not changed, and the new update is prepared
            // when the launcher starts the instance again on next start.
            if (IsTrialBoot()) {
                LOG_INF() << "Rootfs update is in progress, new update is deferred"
                          << Log::Field("state", mUpdateState);
            } else if (err = PrepareUpdate(instance); err.IsNone()) {
                FillInstanceStatus(mPending, status);
            } else {
                err           = AOS_ERROR_WRAP(err);
                status.mState = InstanceStateEnum::eFailed;
                status.mError = err;
            }
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

    Error err;

    {
        std::lock_guard lock {mMutex};

        // Stop is the explicit decision to drop the failed update: the same update is prepared by the next start.
        if (!mHasUpdateState && mPending.mState == InstanceStateEnum::eFailed
            && static_cast<const InstanceIdent&>(mPending) == instance) {
            if (err = RemovePending(); !err.IsNone()) {
                status.mState = InstanceStateEnum::eFailed;
                status.mError = err;
            }
        }
    }

    mStatusReceiver->OnInstancesStatusesReceived(Array<InstanceStatus> {&status, 1});

    return err;
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

            (void)SetPendingFailed(checkErr);

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

    size_t numPending = 0;

    for (const auto& instance : *instances) {
        if (instance.mState == InstanceStateEnum::eActive) {
            mCurrent = instance;
        } else {
            mPending = instance;
            numPending++;
        }
    }

    // Several pending instances are left if a failed pending instance can't be removed and SM is restarted. The tracked
    // one can't be distinguished: all of them are removed, and the update state is processed as untracked.
    if (numPending > 1) {
        LOG_WRN() << "Several pending rootfs instances, remove them" << Log::Field("count", numPending);

        for (const auto& instance : *instances) {
            if (instance.mState == InstanceStateEnum::eActive) {
                continue;
            }

            if (auto err = mStorage->RemoveRuntimeInstance(instance.mRuntimeID, instance, instance.mVersion);
                !err.IsNone() && !err.Is(ErrorEnum::eNotFound)) {
                LOG_ERR() << "Can't remove pending instance" << Log::Field(AOS_ERROR_WRAP(err));
            }
        }

        mPending = {};
    }

    if (!mCurrent.mItemID.IsEmpty()) {
        return ErrorEnum::eNone;
    }

    if (mHasUpdateState && mUpdateState == UpdateStateEnum::eApplied) {
        auto [version, err] = GetCurrentVersion(mRootfsConfig.mVersionFilePath);

        // The version file read error is processed with the applied state: it doesn't fail SM start.
        if (!err.IsNone()) {
            LOG_ERR() << "Can't read applied rootfs version" << Log::Field(err);

            return ErrorEnum::eNone;
        }

        if (!mPending.mItemID.IsEmpty() && version == mPending.mVersion) {
            LOG_WRN() << "Rootfs update promotion is interrupted, repeat promotion";

            return ErrorEnum::eNone;
        }
    }

    // The version file of the trial or partially applied rootfs doesn't match the installed rootfs: the default
    // instance is created after the installed rootfs is booted.
    if (!IsInstalledRootfsBooted()) {
        LOG_WRN() << "Current rootfs instance is not found" << Log::Field("state", mUpdateState);

        return ErrorEnum::eNone;
    }

    LOG_WRN() << "Current rootfs instance is not found, create default";

    return RestoreDefaultCurrent();
}

// State file is written atomically, so it may be invalid only if the filesystem is damaged: invalid state is treated as
// no update.
Error RootfsRuntime::LoadUpdateState()
{
    mHasUpdateState   = false;
    mUpdateState      = UpdateStateEnum::eNumStates;
    mVersionReadError = ErrorEnum::eNone;
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
// before reboot. Applying state means that initramfs failed to apply the update.
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
        return WaitReboot(statuses);

    case UpdateStateEnum::eRejected:
        // SM may be interrupted after rejected state is stored and before the pending instance is updated.
        (void)SetPendingFailed(GetUpdateError("update is rejected"));

        return WaitReboot(statuses);

    case UpdateStateEnum::eApplying:
        return ProcessApplyFailed(statuses);

    case UpdateStateEnum::eApplied:
        return ProcessApplied(statuses);

    case UpdateStateEnum::eReverted:
        return ProcessFailed(statuses, GetUpdateError("update is reverted"));

    default:
        return ProcessFailed(statuses, GetUpdateError("update failed"));
    }
}

// No update state: the update was interrupted before it is committed, or it is finished with failure. The interrupted
// update is reported and removed, so it is prepared again when the launcher starts the instance. The failed update is
// kept: the launcher starts stored instances on every SM start and the CM sends scheduled instances on every balancing,
// so the failed update is prepared again only after the instance is stopped.
Error RootfsRuntime::ProcessNoUpdate(Array<InstanceStatus>& statuses)
{
    // Removes invalid state file, stale images and temporary files.
    if (auto err = ClearUpdateArtifacts(); !err.IsNone()) {
        LOG_ERR() << "Failed to clear update artifacts" << Log::Field(err);
    }

    if (!mPending.mItemID.IsEmpty()) {
        const auto notStarted = mPending.mState != InstanceStateEnum::eFailed;

        // The pending instance stays tracked if it can't be removed: its record conflicts with the next update.
        Error removeErr;

        if (notStarted) {
            removeErr = RemoveFailedPending(AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "update is not started")));
        }

        if (auto err = AddStatus(statuses, mPending); !err.IsNone()) {
            return err;
        }

        if (notStarted && removeErr.IsNone()) {
            mPending = {};
        }
    }

    return AddStatus(statuses, mCurrent);
}

// Update state without pending instance: storage is lost or the promotion is interrupted after storage is updated.
Error RootfsRuntime::ProcessUntracked(Array<InstanceStatus>& statuses)
{
    LOG_WRN() << "Rootfs update without pending instance" << Log::Field("state", mUpdateState);

    if (mUpdateState == UpdateStateEnum::eConfirmed || mUpdateState == UpdateStateEnum::eApplying) {
        // The rootfs may be partially updated, cancellation is not safe: initramfs applies the update, and the current
        // instance is restored from the applied rootfs. Confirmed state means that the trial rootfs is running: reboot
        // is requested to apply the update. Failed apply doesn't request reboot to avoid reboot loop.
        mRebootRequired = mUpdateState == UpdateStateEnum::eConfirmed;

        return AddStatus(statuses, mCurrent);
    }

    // Trial rootfs may be running, but the installed rootfs is not modified: the update is rejected, so initramfs boots
    // the installed rootfs. The state is kept until initramfs reverts the update, so SM restart before reboot requests
    // reboot again. If the rejection can't be stored, initramfs reverts the trial state as well.
    if (mUpdateState == UpdateStateEnum::eTrial || mUpdateState == UpdateStateEnum::eRejected) {
        if (mUpdateState == UpdateStateEnum::eTrial) {
            if (auto err = StoreUpdateState(UpdateStateEnum::eRejected, "update is not tracked"); !err.IsNone()) {
                LOG_ERR() << "Can't reject untracked rootfs update" << Log::Field(err);
            }
        }

        mRebootRequired = true;

        return AddStatus(statuses, mCurrent);
    }

    // Applied state is kept until the current instance is restored: the applied rootfs can't be detected without it.
    if (mUpdateState == UpdateStateEnum::eApplied) {
        auto [version, err] = GetCurrentVersion(mRootfsConfig.mVersionFilePath);
        if (!err.IsNone()) {
            return ProcessVersionReadError(statuses, AOS_ERROR_WRAP(err));
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

    if (auto err = ClearUpdateArtifacts(); !err.IsNone()) {
        LOG_ERR() << "Failed to clear update artifacts" << Log::Field(err);
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

    (void)SetPendingFailed(GetUpdateError(""));

    return AddStatus(statuses, mPending);
}

// On version mismatch the rootfs is already modified: the current instance is restored from the version file, and the
// cloud sends the desired instance again.
Error RootfsRuntime::ProcessApplied(Array<InstanceStatus>& statuses)
{
    auto [version, err] = GetCurrentVersion(mRootfsConfig.mVersionFilePath);
    if (!err.IsNone()) {
        return ProcessVersionReadError(statuses, AOS_ERROR_WRAP(err));
    }

    if (version != mPending.mVersion) {
        LOG_ERR() << "Rootfs version mismatch" << Log::Field("current", version)
                  << Log::Field("expected", mPending.mVersion);

        if (version != mCurrent.mVersion) {
            if (err = RestoreDefaultCurrent(); !err.IsNone()) {
                return AOS_ERROR_WRAP(err);
            }
        }

        return ProcessFailed(statuses, AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "rootfs version mismatch")));
    }

    LOG_INF() << "Rootfs update applied" << Log::Field("ident", static_cast<const InstanceIdent&>(mPending))
              << Log::Field("version", mPending.mVersion);

    auto prevInstance = mCurrent;

    if (err = PromotePending(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    // If cleanup is interrupted, the state is processed as untracked on next start.
    if (auto clearErr = ClearUpdateArtifacts(); !clearErr.IsNone()) {
        LOG_ERR() << "Failed to clear update artifacts" << Log::Field(clearErr);
    }

    // Previous current instance doesn't exist if the previous promotion is interrupted.
    if (!prevInstance.mItemID.IsEmpty()) {
        prevInstance.mState = InstanceStateEnum::eInactive;

        if (auto addErr = AddStatus(statuses, prevInstance); !addErr.IsNone()) {
            return addErr;
        }
    }

    return AddStatus(statuses, mCurrent);
}

// Update is finished without changing the rootfs: the installed rootfs is running. The failure is stored before the
// update state is cleared: if it can't be stored, the state is kept and processed again on next start, so the failed
// update can't be taken for not started one.
Error RootfsRuntime::ProcessFailed(Array<InstanceStatus>& statuses, const Error& err)
{
    LOG_ERR() << "Rootfs update failed" << Log::Field("ident", static_cast<const InstanceIdent&>(mPending))
              << Log::Field("version", mPending.mVersion) << Log::Field(err);

    if (auto storeErr = SetPendingFailed(err); storeErr.IsNone()) {
        if (auto clearErr = ClearUpdateArtifacts(); !clearErr.IsNone()) {
            LOG_ERR() << "Failed to clear update artifacts" << Log::Field(clearErr);
        }
    }

    if (auto addErr = AddStatus(statuses, mPending); !addErr.IsNone()) {
        return addErr;
    }

    return AddStatus(statuses, mCurrent);
}

// The applied rootfs can't be verified without the version file. SM start doesn't fail, so the node stays connected to
// the cloud: the state is kept and processed again on next start, the instances are reported as failed with the error.
Error RootfsRuntime::ProcessVersionReadError(Array<InstanceStatus>& statuses, const Error& err)
{
    LOG_ERR() << "Can't verify applied rootfs, update will be processed on next start" << Log::Field(err);

    // The error is stored with the same text as reported.
    mVersionReadError = Error(err, ErrorToString(err).c_str());

    // The failure is not stored: the applied update is processed again on next start.
    if (!mPending.mItemID.IsEmpty()) {
        mPending.mState = InstanceStateEnum::eFailed;
        mPending.mError = mVersionReadError;

        if (auto addErr = AddStatus(statuses, mPending); !addErr.IsNone()) {
            return addErr;
        }
    }

    if (mCurrent.mItemID.IsEmpty()) {
        return ErrorEnum::eNone;
    }

    if (auto addErr = AddStatus(statuses, mCurrent); !addErr.IsNone()) {
        return addErr;
    }

    statuses.Back().mState = InstanceStateEnum::eFailed;
    statuses.Back().mError = mVersionReadError;

    return ErrorEnum::eNone;
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

    (void)SetPendingFailed(err);

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
Error RootfsRuntime::SetPendingFailed(const Error& err)
{
    // The error is stored with the same text as the update state reason.
    mPending.mState = InstanceStateEnum::eFailed;
    mPending.mError = err.Message() ? Error(err, ErrorToString(err).c_str()) : err;

    if (auto storageErr = mStorage->UpdateRuntimeInstance(mPending); !storageErr.IsNone()) {
        LOG_ERR() << "Can't store failed rootfs update" << Log::Field(AOS_ERROR_WRAP(storageErr));

        return AOS_ERROR_WRAP(storageErr);
    }

    return ErrorEnum::eNone;
}

// The update didn't start: the pending instance is removed from storage. The failure is kept in memory to be reported.
Error RootfsRuntime::RemoveFailedPending(const Error& err)
{
    mPending.mState = InstanceStateEnum::eFailed;
    mPending.mError = err.Message() ? Error(err, ErrorToString(err).c_str()) : err;

    if (auto storageErr = mStorage->RemoveRuntimeInstance(mPending.mRuntimeID, mPending, mPending.mVersion);
        !storageErr.IsNone() && !storageErr.Is(ErrorEnum::eNotFound)) {
        LOG_ERR() << "Can't remove failed rootfs update" << Log::Field(AOS_ERROR_WRAP(storageErr));

        return AOS_ERROR_WRAP(storageErr);
    }

    return ErrorEnum::eNone;
}

// Current instance is missing if it can't be restored before the installed rootfs is booted.
Error RootfsRuntime::AddStatus(Array<InstanceStatus>& statuses, const runtimes::RuntimeInstanceData& instance) const
{
    if (instance.mItemID.IsEmpty()) {
        return ErrorEnum::eNone;
    }

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

    if (auto err = WriteFileDurable(mFSPlatform, GetPath(cUpdateStateFileName), content.str()); !err.IsNone()) {
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

// Fills status if the instance is the current or the pending one. Returns false if the instance is unknown, and the
// update should be prepared. The update error is returned for the failed pending instance: the launcher treats
// successful start as active instance. The failed update is rejected and waits for reboot, its apply failed and
// initramfs retries it on next boot, the applied rootfs can't be verified and is processed again on next start, or it
// is finished and stays failed until the instance is stopped. The current instance is reported with the update error
// as well if the rootfs is unverified.
RetWithError<bool> RootfsRuntime::GetCurrentOrPendingStatus(const InstanceInfo& instance, InstanceStatus& status) const
{
    for (const auto* known : {&mCurrent, &mPending}) {
        if (known->mItemID.IsEmpty()
            || static_cast<const InstanceIdent&>(instance) != static_cast<const InstanceIdent&>(*known)
            || known->mManifestDigest != instance.mManifestDigest) {
            continue;
        }

        FillInstanceStatus(*known, status);

        if (known == &mCurrent && IsRootfsUnverified()) {
            status.mState = InstanceStateEnum::eFailed;
            status.mError = IsApplyFailed() ? GetUpdateError("rootfs update apply failed") : mVersionReadError;

            return {true, status.mError};
        }

        if (known->mState != InstanceStateEnum::eFailed) {
            return true;
        }

        return {
            true, known->mError.IsNone() ? AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "update failed")) : known->mError};
    }

    return false;
}

bool RootfsRuntime::IsApplyFailed() const
{
    return mHasUpdateState && mUpdateState == UpdateStateEnum::eApplying;
}

// The rootfs may be partially updated, or it is applied but its version can't be read.
bool RootfsRuntime::IsRootfsUnverified() const
{
    return IsApplyFailed() || !mVersionReadError.IsNone();
}

// Initramfs always changes confirmed and rejected states on boot, and SM writes them only in trial boot: in these
// states the trial rootfs is running.
bool RootfsRuntime::IsTrialBoot() const
{
    return mHasUpdateState
        && (mUpdateState == UpdateStateEnum::eTrial || mUpdateState == UpdateStateEnum::eConfirmed
            || mUpdateState == UpdateStateEnum::eRejected);
}

// Trial rootfs is the update image, and the rootfs in applying state may be partially updated: in both cases the
// version file doesn't match the installed rootfs.
bool RootfsRuntime::IsInstalledRootfsBooted() const
{
    return !IsTrialBoot() && !IsApplyFailed();
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

    if (!std::filesystem::copy_file(
            imageArchivePath.CStr(), tmpPath, std::filesystem::copy_options::overwrite_existing, ec)) { // NOSONAR
        (void)RemoveFile(tmpPath);

        return AOS_ERROR_WRAP(Error(ec.value(), "can't copy image"));
    }

    if (auto err = VerifyImage(tmpPath, layer.mSize, hash); !err.IsNone()) {
        (void)RemoveFile(tmpPath);

        return AOS_ERROR_WRAP(err);
    }

    if (auto err = CommitFile(mFSPlatform, tmpPath, imagePath); !err.IsNone()) {
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
Error RootfsRuntime::ClearUpdateArtifacts()
{
    if (auto err = AOS_ERROR_WRAP(RemoveFileDurable(mFSPlatform, GetPath(cUpdateStateFileName))); !err.IsNone()) {
        LOG_ERR() << "Failed to remove update state" << Log::Field(err);

        return err;
    }

    // The update is finished: initramfs doesn't act on it anymore.
    mHasUpdateState   = false;
    mUpdateState      = UpdateStateEnum::eNumStates;
    mVersionReadError = ErrorEnum::eNone;

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

    handleErr(AOS_ERROR_WRAP(mFSPlatform.Sync(mRootfsConfig.mWorkingDir.c_str(), false)), "Failed to sync working dir");

    return firstErr;
}

// Pending instance is kept if it can't be removed: it is removed by the next update or on next start.
Error RootfsRuntime::RemovePending()
{
    if (mPending.mItemID.IsEmpty()) {
        return ErrorEnum::eNone;
    }

    if (auto err = mStorage->RemoveRuntimeInstance(mPending.mRuntimeID, mPending, mPending.mVersion);
        !err.IsNone() && !err.Is(ErrorEnum::eNotFound)) {
        return AOS_ERROR_WRAP(err);
    }

    mPending = {};

    return ErrorEnum::eNone;
}

// Only one update is stored. The update in trial boot is finalized before the new one is prepared, so the previous
// update is not started (prepared), or it is finished. No update is prepared over the unverified rootfs: the state and
// the image of the failed apply are needed by initramfs to recover the partially updated rootfs, and removing them
// before the new update is committed leaves the rootfs without recovery if the new update fails. The new update is
// validated first: an invalid one keeps the previous update. Then update artifacts are written in order: the previous
// state file is removed, so initramfs doesn't act on it anymore, the previous images and pending instance are removed,
// the image is copied, the pending instance is stored and the prepared state is written. The state file is the commit
// point: initramfs starts the update only if it exists, and at this moment the image and the pending instance are
// durable. On error update artifacts and the pending instance are removed.
Error RootfsRuntime::PrepareUpdate(const InstanceInfo& instance)
{
    LOG_DBG() << "Preparing update" << Log::Field("ident", static_cast<const InstanceIdent&>(instance));

    if (instance.mItemID == mCurrent.mItemID && instance.mSubjectID == mCurrent.mSubjectID
        && instance.mInstance == mCurrent.mInstance && instance.mType == mCurrent.mType
        && instance.mVersion == mCurrent.mVersion) {
        // The current instance restored from the version file has no manifest digest: the instance is the installed
        // rootfs, the launcher treats this error as an active instance.
        if (mCurrent.mManifestDigest.IsEmpty()) {
            return AOS_ERROR_WRAP(Error(ErrorEnum::eAlreadyExist, "rootfs version is already installed"));
        }

        // Storage key doesn't include manifest digest and preinstalled flag, and the trial boot is verified by version
        // only: a different image with the installed version can't be updated.
        return AOS_ERROR_WRAP(Error(ErrorEnum::eInvalidArgument, "other image with installed rootfs version"));
    }

    if (IsRootfsUnverified()) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eWrongState, "update is not allowed over unverified rootfs"));
    }

    auto        imageManifest = std::make_unique<oci::ImageManifest>();
    std::string updateType;

    auto err = GetImageManifest(instance.mManifestDigest, *imageManifest);

    if (err.IsNone()) {
        Tie(updateType, err) = GetUpdateType(*imageManifest);
    }

    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    err = ClearUpdateArtifacts();

    if (err.IsNone()) {
        err = RemovePending();
    }

    if (err.IsNone()) {
        err = CopyImage(*imageManifest);
    }

    if (err.IsNone()) {
        runtimes::RuntimeInstanceData pending;

        static_cast<InstanceIdent&>(pending) = static_cast<const InstanceIdent&>(instance);
        pending.mRuntimeID                   = mRuntimeInfo.mRuntimeID;
        pending.mManifestDigest              = instance.mManifestDigest;
        pending.mVersion                     = instance.mVersion;
        pending.mState                       = InstanceStateEnum::eActivating;

        err = mStorage->AddRuntimeInstance(pending);
        if (err.IsNone()) {
            mPending = pending;
        }
    }

    if (err.IsNone()) {
        mUpdateType = updateType;
        err         = StoreUpdateState(UpdateStateEnum::ePrepared);
    }

    if (!err.IsNone()) {
        // The pending instance is kept if the state file can't be removed: the update stays tracked and is processed
        // on next start.
        if (auto clearErr = ClearUpdateArtifacts(); !clearErr.IsNone()) {
            LOG_ERR() << "Failed to clear update artifacts" << Log::Field(clearErr);
        } else if (auto removeErr = RemovePending(); !removeErr.IsNone()) {
            LOG_ERR() << "Can't remove pending instance" << Log::Field(removeErr);
        }

        // The previous prepared update is kept if its state file can't be removed.
        mRebootRequired = mHasUpdateState && mUpdateState == UpdateStateEnum::ePrepared;

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
