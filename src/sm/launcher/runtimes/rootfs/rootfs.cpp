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
#include <sstream>
#include <string_view>

#include <Poco/String.h>
#include <Poco/StringTokenizer.h>

#include <core/common/crypto/itf/hash.hpp>
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
constexpr auto cDigestSeparator            = ":";
constexpr auto cImageExtension             = ".squashfs";
constexpr auto cImageFileName              = "image.squashfs";
constexpr auto cTmpExtension               = ".tmp";
constexpr auto cDoUpdateFlag               = "do_update";
constexpr auto cUpdatedFlag                = "updated";
constexpr auto cDoApplyFlag                = "do_apply";
constexpr auto cFailedFlag                 = "failed";
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

Error SyncFd(int32_t fd, const char* msg)
{
    Error err;

    if (fsync(fd) != 0) {
        err = Error(errno, msg);
    }

    KeepFirstError(err, CloseFile(fd));

    return err;
}

Error SyncFile(const std::filesystem::path& path)
{
    auto fd = open(path.c_str(), O_RDONLY | O_CLOEXEC); // NOSONAR
    if (fd < 0) {
        return Error(errno, "can't open file");
    }

    return SyncFd(fd, "can't sync file");
}

Error SyncDir(const std::filesystem::path& dir)
{
    auto fd = open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC); // NOSONAR
    if (fd < 0) {
        return Error(errno, "can't open dir");
    }

    return SyncFd(fd, "can't sync dir");
}

Error RemoveFile(const std::filesystem::path& path)
{
    if (unlink(path.c_str()) != 0 && errno != ENOENT) { // NOSONAR
        return Error(errno, "can't remove file");
    }

    return ErrorEnum::eNone;
}

// Syncs temporary file, atomically renames it to the destination path and syncs parent directory. On error temporary
// file is removed.
Error CommitFile(const std::filesystem::path& tmpPath, const std::filesystem::path& path)
{
    auto err = SyncFile(tmpPath);

    if (err.IsNone() && rename(tmpPath.c_str(), path.c_str()) != 0) { // NOSONAR
        err = Error(errno, "can't rename file");
    }

    if (!err.IsNone()) {
        (void)RemoveFile(tmpPath);

        return err;
    }

    return SyncDir(path.parent_path());
}

Error WriteFileDurable(const std::filesystem::path& path, const std::string& content)
{
    const auto tmpPath = GetTmpPath(path);

    {
        std::ofstream file(tmpPath, std::ios::trunc); // NOSONAR
        if (!file.is_open()) {
            return Error(ErrorEnum::eFailed, "can't create file");
        }

        file << content;

        if (file.close(); file.fail()) {
            (void)RemoveFile(tmpPath);

            return Error(ErrorEnum::eFailed, "can't write file");
        }
    }

    std::error_code ec;

    std::filesystem::permissions(tmpPath, static_cast<std::filesystem::perms>(cFilePerm), ec);
    if (ec) {
        (void)RemoveFile(tmpPath);

        return Error(ec.value(), "can't set file permissions");
    }

    return CommitFile(tmpPath, path);
}

// Lookup error should not be treated as missing file, otherwise the update being applied is canceled.
RetWithError<bool> FileExists(const std::filesystem::path& path)
{
    std::error_code ec;

    const auto exists = std::filesystem::exists(path, ec);
    if (ec) {
        return {false, Error(ec.value(), "can't check update file")};
    }

    return exists;
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

// Digest format: <algorithm>:<hex encoded hash>. Only sha256 is supported.
Error ParseSHA256Digest(const String& digest, StaticArray<uint8_t, crypto::cSHA256Size>& hash)
{
    auto [pos, err] = digest.FindSubstr(0, cDigestSeparator);
    if (!err.IsNone()) {
        return Error(ErrorEnum::eInvalidArgument, "invalid digest format");
    }

    StaticString<oci::cDigestLen> alg;
    StaticString<oci::cDigestLen> hexHash;

    if (err = alg.Insert(alg.end(), digest.begin(), digest.begin() + pos); !err.IsNone()) {
        return err;
    }

    if (alg.Compare(cSHA256Algorithm, String::CaseSensitivity::CaseInsensitive) != 0) {
        return Error(ErrorEnum::eNotSupported, "unsupported digest algorithm");
    }

    if (err = hexHash.Insert(hexHash.end(), digest.begin() + pos + 1, digest.end()); !err.IsNone()) {
        return err;
    }

    if (err = hexHash.HexToByteArray(hash); !err.IsNone() || hash.Size() != crypto::cSHA256Size) {
        return Error(ErrorEnum::eInvalidArgument, "invalid digest hash");
    }

    return ErrorEnum::eNone;
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

        if (auto err = LoadInstances(); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (auto err = LoadUpdateFlags(); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (auto err = ProcessUpdate(*statuses); !err.IsNone()) {
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

// The verdict is stored in storage and in the update flags: on success do_apply flag is written, so initramfs applies
// the update on next boot. On failure or if do_apply can't be written, the pending instance is failed and initramfs
// reverts the update on next boot. Reboot is requested in any case.
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

        if (mPending.mItemID.IsEmpty() || mPending.mState != InstanceStateEnum::eActivating || !mFlags.mUpdated
            || mFlags.mDoApply
            || static_cast<const InstanceIdent&>(mPending) != static_cast<const InstanceIdent&>(*status)
            || mPending.mManifestDigest != status->mManifestDigest) {
            LOG_WRN() << "Pending rootfs update is changed, health check verdict is discarded";

            return;
        }

        if (checkErr.IsNone()) {
            if (auto err = WriteFileDurable(GetPath(cDoApplyFlag), ""); !err.IsNone()) {
                LOG_ERR() << "Can't confirm rootfs update" << Log::Field(err);

                checkErr = AOS_ERROR_WRAP(err);
            } else {
                mFlags.mDoApply = true;
            }
        }

        if (!checkErr.IsNone()) {
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

// Storage keeps the current (active) instance and the pending one. The current instance may be missing on first start,
// after storage loss or if the promotion is interrupted: it is created from the version file when the installed rootfs
// is reported.
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

    return ErrorEnum::eNone;
}

Error RootfsRuntime::LoadUpdateFlags()
{
    mFlags = {};

    const std::pair<const char*, bool*> flags[] = {
        {cDoUpdateFlag, &mFlags.mDoUpdate},
        {cUpdatedFlag, &mFlags.mUpdated},
        {cDoApplyFlag, &mFlags.mDoApply},
        {cFailedFlag, &mFlags.mFailed},
    };

    for (const auto& [name, value] : flags) {
        auto [exists, err] = FileExists(GetPath(name));
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        *value = exists;
    }

    if (mFlags.mFailed) {
        std::ifstream file(GetPath(cFailedFlag)); // NOSONAR
        std::string   reason((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        mFlags.mFailReason = Poco::trim(reason);
    }

    LOG_DBG() << "Rootfs update flags" << Log::Field("doUpdate", mFlags.mDoUpdate)
              << Log::Field("updated", mFlags.mUpdated) << Log::Field("doApply", mFlags.mDoApply)
              << Log::Field("failed", mFlags.mFailed);

    return ErrorEnum::eNone;
}

// Initramfs handles the flags on boot in the following order: do_apply - applies the update and clears all flags,
// updated - reverts the update (clears all flags and writes failed), do_update - mounts the update image and creates
// updated. On error initramfs writes failed and keeps other flags.
Error RootfsRuntime::ProcessUpdate(Array<InstanceStatus>& statuses)
{
    if (mPending.mItemID.IsEmpty()) {
        return ProcessNoPending(statuses);
    }

    LOG_DBG() << "Process rootfs update" << Log::Field("ident", static_cast<const InstanceIdent&>(mPending))
              << Log::Field("version", mPending.mVersion) << Log::Field("state", mPending.mState);

    if (mPending.mState == InstanceStateEnum::eFailed) {
        return ProcessPendingFailed(statuses);
    }

    if (mFlags.mFailed) {
        return ProcessInitramfsFailed(statuses);
    }

    // SM is restarted after the update is prepared or confirmed but before reboot.
    if (mFlags.mDoApply || (mFlags.mDoUpdate && !mFlags.mUpdated)) {
        return WaitReboot(statuses);
    }

    if (mFlags.mUpdated) {
        return ProcessTrial(statuses);
    }

    return ProcessFinished(statuses);
}

// Update flags without pending instance: storage is lost or artifacts cleanup is interrupted.
Error RootfsRuntime::ProcessNoPending(Array<InstanceStatus>& statuses)
{
    if (mFlags.mDoApply) {
        // The rootfs may be partially updated, cancellation is not safe: initramfs applies the update, and the current
        // instance is restored from the applied rootfs on next start.
        LOG_WRN() << "Rootfs update is applied without pending instance";

        return AddCurrentStatus(statuses, false);
    }

    // Trial rootfs may be running: after reboot without update flags the installed rootfs is booted.
    mRebootRequired = mFlags.mUpdated;

    if (auto err = ClearUpdateArtifacts(); !err.IsNone()) {
        LOG_ERR() << "Failed to clear update artifacts" << Log::Field(err);
    }

    return AddCurrentStatus(statuses, !mRebootRequired);
}

// The failure is already stored. If the update image is still running as rootfs, initramfs reverts it on next boot.
Error RootfsRuntime::ProcessPendingFailed(Array<InstanceStatus>& statuses)
{
    if (mFlags.mUpdated && !mFlags.mFailed) {
        return WaitReboot(statuses);
    }

    if (auto err = ClearUpdateArtifacts(); !err.IsNone()) {
        LOG_ERR() << "Failed to clear update artifacts" << Log::Field(err);
    }

    if (auto err = AddStatus(statuses, mPending); !err.IsNone()) {
        return err;
    }

    return AddCurrentStatus(statuses);
}

Error RootfsRuntime::ProcessInitramfsFailed(Array<InstanceStatus>& statuses)
{
    const auto err
        = Error(ErrorEnum::eFailed, mFlags.mFailReason.empty() ? "update failed" : mFlags.mFailReason.c_str());

    if (!mFlags.mDoApply) {
        return ProcessFailed(statuses, err);
    }

    // The rootfs may be partially updated: do_apply flag is kept, so initramfs retries the apply on next boot. The
    // failure is reported but not stored: the update is completed by the retried apply. Reboot is not requested to
    // avoid reboot loop.
    LOG_ERR() << "Rootfs update apply failed, apply will be retried on next boot" << Log::Field(err);

    mPending.mState = InstanceStateEnum::eFailed;
    mPending.mError = err;

    return AddStatus(statuses, mPending);
}

// Initramfs mounted the update image as rootfs. SM restart before the verdict is stored runs the health check again.
Error RootfsRuntime::ProcessTrial(Array<InstanceStatus>& statuses)
{
    if (auto err = VerifyRootfsVersion(mRootfsConfig.mVersionFilePath, mPending.mVersion); !err.IsNone()) {
        LOG_ERR() << "Rootfs trial boot verification failed" << Log::Field(err);

        SetPendingFailed(err);

        return WaitReboot(statuses);
    }

    if (auto err = AddStatus(statuses, mPending); !err.IsNone()) {
        return err;
    }

    // Health check is started by Start after statuses are sent.
    mRunHealthCheck = true;

    return ErrorEnum::eNone;
}

// No update flags: initramfs either applied the update or the update didn't happen (interrupted before do_update is
// written or reverted before failed flag is written). The running rootfs version tells which one.
Error RootfsRuntime::ProcessFinished(Array<InstanceStatus>& statuses)
{
    auto [version, err] = GetCurrentVersion(mRootfsConfig.mVersionFilePath);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (version != mPending.mVersion) {
        return ProcessFailed(statuses, AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "update is not applied")));
    }

    LOG_INF() << "Rootfs update applied" << Log::Field("ident", static_cast<const InstanceIdent&>(mPending))
              << Log::Field("version", mPending.mVersion);

    auto prevInstance = mCurrent;

    if (err = PromotePending(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (err = ClearUpdateArtifacts(); !err.IsNone()) {
        LOG_ERR() << "Failed to clear update artifacts" << Log::Field(err);
    }

    if (!prevInstance.mItemID.IsEmpty()) {
        prevInstance.mState = InstanceStateEnum::eInactive;

        if (err = AddStatus(statuses, prevInstance); !err.IsNone()) {
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

    return AddCurrentStatus(statuses);
}

Error RootfsRuntime::WaitReboot(Array<InstanceStatus>& statuses)
{
    LOG_INF() << "Rootfs update is waiting for reboot" << Log::Field("state", mPending.mState);

    mRebootRequired = true;

    return AddStatus(statuses, mPending);
}

// The previous current instance is removed first: if the promotion is interrupted, only the pending instance is left
// in storage and the promotion is repeated on next start.
Error RootfsRuntime::PromotePending()
{
    if (!mCurrent.mItemID.IsEmpty()) {
        if (auto err = mStorage->RemoveRuntimeInstance(mCurrent.mRuntimeID, mCurrent, mCurrent.mVersion);
            !err.IsNone() && !err.Is(ErrorEnum::eNotFound)) {
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

// Adds the current instance status. If the installed rootfs is running, the current instance should match its version:
// otherwise (first start, storage loss, interrupted promotion) it is replaced with the default one.
Error RootfsRuntime::AddCurrentStatus(Array<InstanceStatus>& statuses, bool verifyVersion)
{
    if (verifyVersion || mCurrent.mItemID.IsEmpty()) {
        auto [version, err] = GetCurrentVersion(mRootfsConfig.mVersionFilePath);
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (mCurrent.mItemID.IsEmpty() || (verifyVersion && version != mCurrent.mVersion)) {
            if (err = RestoreDefaultCurrent(version); !err.IsNone()) {
                return AOS_ERROR_WRAP(err);
            }
        }
    }

    return AddStatus(statuses, mCurrent);
}

// The previous current instance is removed first: if interrupted, there is no current instance and the default one is
// created on next start.
Error RootfsRuntime::RestoreDefaultCurrent(const String& version)
{
    LOG_WRN() << "Restore default rootfs instance" << Log::Field("version", version);

    if (!mCurrent.mItemID.IsEmpty()) {
        if (auto err = mStorage->RemoveRuntimeInstance(mCurrent.mRuntimeID, mCurrent, mCurrent.mVersion);
            !err.IsNone() && !err.Is(ErrorEnum::eNotFound)) {
            return AOS_ERROR_WRAP(err);
        }

        mCurrent = {};
    }

    runtimes::RuntimeInstanceData instance;

    static_cast<InstanceIdent&>(instance) = mDefaultInstanceIdent;
    instance.mRuntimeID                   = mRuntimeInfo.mRuntimeID;
    instance.mVersion                     = version;
    instance.mState                       = InstanceStateEnum::eActive;

    if (auto err = mStorage->AddRuntimeInstance(instance); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    mCurrent = instance;

    return ErrorEnum::eNone;
}

// Storage errors don't stop the update processing: the failure is reported, and the update is processed again on next
// start.
void RootfsRuntime::SetPendingFailed(const Error& err)
{
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

// Image is copied to temporary file, synced, verified against manifest layer descriptor and atomically renamed.
// Temporary file extension doesn't match initramfs image search pattern, so partially copied image is never used for
// update.
Error RootfsRuntime::CopyImage(const oci::ContentDescriptor& layer) const
{
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

    const auto      path    = GetPath(cImageFileName);
    const auto      tmpPath = GetTmpPath(path);
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

    if (auto err = CommitFile(tmpPath, path); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error RootfsRuntime::VerifyImage(const std::filesystem::path& path, uint64_t size, const Array<uint8_t>& hash) const
{
    fs::FileInfo fileInfo;

    if (auto err = mFileInfoProvider->GetFileInfo(path.c_str(), fileInfo); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (fileInfo.mSize != size) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eInvalidChecksum, "image size mismatch"));
    }

    if (fileInfo.mCheckSum != hash) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eInvalidChecksum, "image digest mismatch"));
    }

    return ErrorEnum::eNone;
}

// Update flags are removed first and cleanup stops if it fails: the image must never be removed while do_update or
// do_apply flag remains. do_apply is removed first as it modifies the installed rootfs. Images and temporary files
// removal errors don't stop cleanup, the first error is returned.
Error RootfsRuntime::ClearUpdateArtifacts() const
{
    for (const auto* flag : {cDoApplyFlag, cDoUpdateFlag, cUpdatedFlag, cFailedFlag}) {
        if (auto err = AOS_ERROR_WRAP(RemoveFile(GetPath(flag))); !err.IsNone()) {
            LOG_ERR() << "Failed to remove update flag" << Log::Field("flag", flag) << Log::Field(err);

            return err;
        }
    }

    if (auto err = AOS_ERROR_WRAP(SyncDir(mRootfsConfig.mWorkingDir)); !err.IsNone()) {
        LOG_ERR() << "Failed to sync working dir" << Log::Field(err);

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

// Update artifacts are written in order: image, pending instance in storage, do_update flag. The do_update flag is the
// commit point: initramfs starts the update only if it exists, and at this moment the image and the pending instance
// are durable. On error update artifacts and the pending instance are removed.
Error RootfsRuntime::PrepareUpdate(const InstanceInfo& instance)
{
    LOG_DBG() << "Preparing update" << Log::Field("ident", static_cast<const InstanceIdent&>(instance));

    // Flags of the previous update are removed first, so initramfs doesn't act on them anymore.
    if (auto err = ClearUpdateArtifacts(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    mFlags = {};

    if (!mPending.mItemID.IsEmpty()) {
        if (auto err = mStorage->RemoveRuntimeInstance(mPending.mRuntimeID, mPending, mPending.mVersion);
            !err.IsNone() && !err.Is(ErrorEnum::eNotFound)) {
            return AOS_ERROR_WRAP(err);
        }
    }

    ResetPending();

    auto        imageManifest = std::make_unique<oci::ImageManifest>();
    auto        pendingAdded  = false;
    std::string updateType;

    auto err = GetImageManifest(instance.mManifestDigest, *imageManifest);

    if (err.IsNone()) {
        Tie(updateType, err) = GetUpdateType(*imageManifest);
    }

    if (err.IsNone()) {
        err = CopyImage(imageManifest->mLayers[0]);
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
        err = WriteFileDurable(GetPath(cDoUpdateFlag), updateType);
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

    mFlags.mDoUpdate = true;
    mRebootRequired  = true;

    return ErrorEnum::eNone;
}

std::filesystem::path RootfsRuntime::GetPath(const std::string& fileName) const
{
    return std::filesystem::path(mRootfsConfig.mWorkingDir) / fileName;
}

} // namespace aos::sm::launcher
