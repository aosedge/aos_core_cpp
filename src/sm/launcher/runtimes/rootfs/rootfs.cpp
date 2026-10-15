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
#include <vector>

#include <Poco/DigestEngine.h>
#include <Poco/SHA2Engine.h>
#include <Poco/String.h>
#include <Poco/StringTokenizer.h>

#include <core/common/tools/fs.hpp>
#include <core/common/tools/logger.hpp>

#include <common/utils/exception.hpp>
#include <common/utils/json.hpp>
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
constexpr auto cCopyBufferSize             = 64 * 1024;
constexpr auto cSHA256Algorithm            = "sha256";
constexpr auto cImageExtension             = ".squashfs";
constexpr auto cImageFileName              = "image.squashfs";
constexpr auto cTmpExtension               = ".tmp";
constexpr auto cMediaTypeAppPrefix         = "application/";
constexpr auto cFullMediaTypePrefix        = "vnd.aos.image.component.full";
constexpr auto cIncrementalMediaTypePrefix = "vnd.aos.image.component.inc";
constexpr auto cMediaTypeVersionPrefix     = ".v";
constexpr auto cMediaTypeSuffixSeparator   = '+';
constexpr auto cBootIDFilePath             = "/proc/sys/kernel/random/boot_id";
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

    if (auto closeErr = CloseFile(fd); !closeErr.IsNone() && err.IsNone()) {
        err = closeErr;
    }

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

    if (auto closeErr = CloseFile(fd); !closeErr.IsNone() && err.IsNone()) {
        err = closeErr;
    }

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

Error CopyFileDurable(const std::filesystem::path& src, const std::filesystem::path& dst, size_t expectedSize,
    std::string_view expectedHash)
{
    int32_t srcFd = open(src.c_str(), O_RDONLY | O_CLOEXEC); // NOSONAR
    if (srcFd < 0) {
        return Error(errno, "can't open source file");
    }

    auto closeSrc = DeferRelease(&srcFd, [](const int32_t* fd) { (void)CloseFile(*fd); });

    const auto tmpPath = GetTmpPath(dst);

    int32_t dstFd = open(tmpPath.c_str(), O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, cFilePerm); // NOSONAR
    if (dstFd < 0) {
        return Error(errno, "can't create destination file");
    }

    auto failCopy = [dstFd, &tmpPath](const Error& err) {
        (void)CloseFile(dstFd);
        (void)RemoveFile(tmpPath);

        return err;
    };

    Poco::SHA2Engine  hashEngine;
    std::vector<char> buffer(cCopyBufferSize);
    size_t            size = 0;

    while (true) {
        auto readSize = read(srcFd, buffer.data(), buffer.size());
        if (readSize < 0) {
            if (errno == EINTR) {
                continue;
            }

            return failCopy(Error(errno, "can't read source file"));
        }

        if (readSize == 0) {
            break;
        }

        hashEngine.update(buffer.data(), readSize);

        if (auto err = WriteAll(dstFd, buffer.data(), readSize); !err.IsNone()) {
            return failCopy(err);
        }

        size += readSize;
    }

    if (size != expectedSize) {
        return failCopy(Error(ErrorEnum::eInvalidChecksum, "image size mismatch"));
    }

    if (Poco::DigestEngine::digestToHex(hashEngine.digest()) != Poco::toLower(std::string(expectedHash))) {
        return failCopy(Error(ErrorEnum::eInvalidChecksum, "image digest mismatch"));
    }

    return CommitFile(dstFd, tmpPath, dst);
}

Error ParseSHA256Digest(const String& digest, std::string& hash)
{
    const std::string digestStr = digest.CStr();
    const auto        pos       = digestStr.find(':');

    if (pos == std::string::npos) {
        return Error(ErrorEnum::eInvalidArgument, "invalid digest format");
    }

    if (Poco::icompare(digestStr.substr(0, pos), cSHA256Algorithm) != 0) {
        return Error(ErrorEnum::eNotSupported, "unsupported digest algorithm");
    }

    hash = digestStr.substr(pos + 1);

    return ErrorEnum::eNone;
}

// InstanceInfo is too big to be created on stack.
void ResetInstanceInfo(InstanceInfo& instance)
{
    instance = *std::make_unique<InstanceInfo>();
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

RetWithError<std::string> GetBootID()
{
    std::ifstream file(cBootIDFilePath);
    std::string   bootID;

    if (!file.is_open() || !std::getline(file, bootID) || Poco::trim(bootID).empty()) {
        return {"", Error(ErrorEnum::eNotFound, "can't read boot ID")};
    }

    return Poco::trim(bootID);
}

Error SaveInstanceInfo(const InstanceInfo& instance, const std::filesystem::path& path, const std::string& bootID = "",
    bool confirmed = false)
{
    LOG_DBG() << "Save instance info" << Log::Field("ident", static_cast<const InstanceIdent&>(instance))
              << Log::Field("path", path.c_str());

    auto               json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);
    std::ostringstream content;

    try {
        (void)json->set("itemId", instance.mItemID.CStr());
        (void)json->set("subjectId", instance.mSubjectID.CStr());
        (void)json->set("instance", instance.mInstance);
        (void)json->set("manifestDigest", instance.mManifestDigest.CStr());
        (void)json->set("type", instance.mType.ToString().CStr());
        (void)json->set("version", instance.mVersion.CStr());
        (void)json->set("preinstalled", instance.mPreinstalled);

        (void)json->set("bootId", bootID);
        (void)json->set("confirmed", confirmed);

        json->stringify(content);
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    if (auto err = WriteFileDurable(path, content.str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error LoadInstanceInfo(
    const std::filesystem::path& path, InstanceInfo& instance, std::string* bootID = nullptr, bool* confirmed = nullptr)
{
    LOG_DBG() << "Load instance info" << Log::Field("path", path.c_str());

    instance.mType = UpdateItemTypeEnum::eComponent;

    std::ifstream file(path);

    if (!file.is_open()) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eNotFound, "can't open instance info file"));
    }

    try {
        auto parseResult = common::utils::ParseJson(file);
        AOS_ERROR_CHECK_AND_THROW(parseResult.mError);

        auto jsonObject = common::utils::CaseInsensitiveObjectWrapper(parseResult.mValue);

        auto err = instance.mItemID.Assign(jsonObject.GetValue<std::string>("itemId").c_str());
        AOS_ERROR_CHECK_AND_THROW(err);

        err = instance.mSubjectID.Assign(jsonObject.GetValue<std::string>("subjectId").c_str());
        AOS_ERROR_CHECK_AND_THROW(err);

        instance.mInstance = jsonObject.GetValue<uint64_t>("instance");

        err = instance.mManifestDigest.Assign(jsonObject.GetValue<std::string>("manifestDigest").c_str());
        AOS_ERROR_CHECK_AND_THROW(err);

        err = instance.mVersion.Assign(jsonObject.GetValue<std::string>("version").c_str());
        AOS_ERROR_CHECK_AND_THROW(err);

        instance.mType         = UpdateItemTypeEnum::eComponent;
        instance.mPreinstalled = jsonObject.GetValue<bool>("preinstalled");

        if (bootID) {
            *bootID = jsonObject.GetValue<std::string>("bootId");
        }

        if (confirmed) {
            *confirmed = jsonObject.GetValue<bool>("confirmed");
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    if (instance.mItemID.IsEmpty() || instance.mVersion.IsEmpty()) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eInvalidArgument, "instance info is incomplete"));
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
    InstanceStatusReceiverItf& statusReceiver, sm::utils::SystemdConnItf& systemdConn)
{
    LOG_DBG() << "Init runtime" << Log::Field("type", config.mType.c_str());

    mRuntimeConfig           = config;
    mCurrentNodeInfoProvider = &currentNodeInfoProvider;
    mItemInfoProvider        = &itemInfoProvider;
    mOCISpec                 = &ociSpec;
    mStatusReceiver          = &statusReceiver;

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

    // Failed pending instance, previous installed instance and current instance.
    auto statuses       = std::make_unique<StaticArray<InstanceStatus, 3>>();
    auto rebootRequired = false;
    auto runHealthCheck = false;

    {
        std::lock_guard lock {mMutex};

        LOG_DBG() << "Start runtime";

        if (auto err = InitInstalledData(); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (auto err = InitPendingData(); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (auto err = ProcessUpdateAction(*statuses); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        rebootRequired  = mRebootRequired;
        runHealthCheck  = mRunHealthCheck;
        mRunHealthCheck = false;
    }

    // Status receiver is called outside of runtime lock: the launcher calls the runtime under its own lock.
    if (rebootRequired) {
        if (auto err = SendRebootRequest(); !err.IsNone() && statuses->Back().mError.IsNone()) {
            statuses->Back().mError = err;
        }
    }

    mStatusReceiver->OnInstancesStatusesReceived(*statuses);

    // Health check is started after start statuses are sent, so its result is always received after them.
    if (runHealthCheck) {
        std::lock_guard lock {mHealthCheckMutex};

        mHealthCheckThread.emplace(
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
        mStatusReceiver->OnInstancesStatusesReceived(Array<InstanceStatus> {status, 1});
    });

    std::unique_lock lock {mMutex};

    auto [found, err] = GetCurrentOrPendingStatus(instance, status);

    if (!found) {
        lock.unlock();

        FillInstanceStatus(instance, InstanceStateEnum::eActivating, status);

        mStatusReceiver->OnInstancesStatusesReceived(Array<InstanceStatus> {&status, 1});

        // Health check verdict should be stored before update artifacts are replaced by the new update.
        JoinHealthCheck();

        lock.lock();

        Tie(found, err) = GetCurrentOrPendingStatus(instance, status);
    }

    if (!found) {
        if (err = PrepareUpdate(instance); !err.IsNone()) {
            status.mState = InstanceStateEnum::eFailed;
            status.mError = err;

            return AOS_ERROR_WRAP(err);
        }
    }

    const auto rebootRequired = mRebootRequired;

    lock.unlock();

    if (!rebootRequired) {
        return err;
    }

    // Prepared update is kept if reboot request fails: the request is retried on next start instance request.
    if (auto rebootErr = SendRebootRequest(); !rebootErr.IsNone()) {
        if (status.mError.IsNone()) {
            status.mError = rebootErr;
        }

        if (err.IsNone()) {
            err = rebootErr;
        }
    }

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

void RootfsRuntime::RunHealthCheck(std::unique_ptr<InstanceStatus> status)
{
    LOG_DBG() << "Start health check for rootfs update";

    auto checkErr = mUpdateChecker.Check();
    if (!checkErr.IsNone()) {
        LOG_ERR() << "Rootfs update health check failed" << Log::Field(checkErr);

        checkErr = AOS_ERROR_WRAP(checkErr);
    }

    auto rebootAllowed = true;

    {
        std::lock_guard lock {mMutex};

        if (!mHasPending
            || static_cast<const InstanceIdent&>(mPendingInstance) != static_cast<const InstanceIdent&>(*status)
            || mPendingInstance.mManifestDigest != status->mManifestDigest) {
            LOG_WRN() << "Pending rootfs update is changed, health check verdict is discarded";

            return;
        }

        if (auto err = StoreVerdict(checkErr.IsNone(), checkErr); !err.IsNone()) {
            LOG_ERR() << "Can't store rootfs update verdict" << Log::Field(err);

            if (checkErr.IsNone()) {
                // Without do_apply initramfs reverts the update on next boot, so the update is failed anyway.
                checkErr = AOS_ERROR_WRAP(err);

                if (auto storeErr = StoreVerdict(false, checkErr); !storeErr.IsNone()) {
                    LOG_ERR() << "Can't store rootfs update failure" << Log::Field(storeErr);
                }
            }

            // do_apply may remain if the verdict can't be stored: reboot would apply the update reported as failed.
            // Reboot is allowed only if do_apply is durably removed: do_apply absence doesn't mean that its removal is
            // synced. Otherwise reboot is postponed, the update state is processed again on SM restart.
            if (auto removeErr
                = AOS_ERROR_WRAP(RemoveFileDurable(GetPath(ActionType(ActionTypeEnum::eDoApply).ToString().CStr())));
                !removeErr.IsNone()) {
                LOG_ERR() << "Can't remove rootfs update do_apply, reboot is postponed" << Log::Field(removeErr);

                rebootAllowed = false;
            }
        }

        if (!checkErr.IsNone()) {
            mPendingState = InstanceStateEnum::eFailed;
            mPendingError = checkErr;

            status->mState = InstanceStateEnum::eFailed;
            status->mError = checkErr;
        }

        // Reboot request is retried on start instance request if it fails.
        mRebootRequired = rebootAllowed;
    }

    // Status receiver is called outside of runtime lock: the launcher calls the runtime under its own lock.
    if (rebootAllowed) {
        if (auto err = SendRebootRequest(); !err.IsNone() && status->mError.IsNone()) {
            status->mError = err;
        }
    }

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

Error RootfsRuntime::InitInstalledData()
{
    mCurrentError = ErrorEnum::eNone;

    const auto      path = GetPath(cInstalledInstanceFileName);
    std::error_code ec;

    // Lookup error should not be treated as missing file, otherwise installed instance is overwritten.
    const auto exists = std::filesystem::exists(path, ec);
    if (ec) {
        return AOS_ERROR_WRAP(Error(ec.value(), "can't check installed instance file"));
    }

    if (exists) {
        auto err = LoadInstanceInfo(path, mCurrentInstance);
        if (err.IsNone()) {
            return ErrorEnum::eNone;
        }

        LOG_ERR() << "Installed instance info is corrupted, restore default" << Log::Field(err);
    }

    auto [version, err] = GetCurrentVersion(mRootfsConfig.mVersionFilePath);
    if (!err.IsNone()) {
        return err;
    }

    ResetInstanceInfo(mCurrentInstance);

    static_cast<InstanceIdent&>(mCurrentInstance) = mDefaultInstanceIdent;
    mCurrentInstance.mVersion                     = version;

    Actions actions;

    if (auto actionsErr = ReadActions(actions); !actionsErr.IsNone()) {
        return AOS_ERROR_WRAP(actionsErr);
    }

    // During trial boot the version file belongs to the trial rootfs, and after failed apply the rootfs may be
    // partially updated: the version is not stored as installed one. Installed instance is restored on start when the
    // installed rootfs is running.
    if (actions.mUpdated || actions.mDoApply) {
        LOG_WRN() << "Rootfs update is in progress, installed instance is not restored"
                  << Log::Field("version", version);

        return ErrorEnum::eNone;
    }

    return SaveInstanceInfo(mCurrentInstance, path);
}

Error RootfsRuntime::InitPendingData()
{
    ResetPending();

    const auto      path = GetPath(cPendingInstanceFileName);
    std::error_code ec;

    const auto exists = std::filesystem::exists(path, ec);
    if (ec) {
        return AOS_ERROR_WRAP(Error(ec.value(), "can't check pending instance file"));
    }

    if (!exists) {
        return ErrorEnum::eNone;
    }

    if (auto err = LoadInstanceInfo(path, mPendingInstance, &mPendingBootID, &mPendingConfirmed); !err.IsNone()) {
        LOG_ERR() << "Pending instance info is corrupted" << Log::Field(err);

        ResetPending();

        return ErrorEnum::eNone;
    }

    mHasPending = true;

    return ErrorEnum::eNone;
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

// Action files priority: do_apply, updated, failed, do_update. It follows initramfs aosupdate module, failed is handled
// before do_update as initramfs keeps do_update when the update fails. failed together with do_apply or updated is
// handled as waiting for reboot or failed apply.
Error RootfsRuntime::ProcessUpdateAction(Array<InstanceStatus>& statuses)
{
    mApplyIncomplete = false;

    Actions actions;

    if (auto err = ReadActions(actions); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    LOG_DBG() << "Process rootfs update" << Log::Field("hasPending", mHasPending)
              << Log::Field("updated", actions.mUpdated) << Log::Field("doApply", actions.mDoApply)
              << Log::Field("doUpdate", actions.mDoUpdate) << Log::Field("failed", actions.mFailed);

    if (!mHasPending) {
        return ProcessNoPending(statuses, actions);
    }

    // do_apply or updated with failed: health check rejected the update before reboot, or initramfs failed to apply
    // the update after reboot.
    if (actions.mDoApply || actions.mUpdated) {
        if (actions.mFailed) {
            mPendingError = ReadFailedReason();

            return ProcessWaitReboot(statuses, InstanceStateEnum::eFailed, actions);
        }

        if (actions.mDoApply) {
            return ProcessWaitReboot(statuses, InstanceStateEnum::eActivating, actions);
        }

        return ProcessUpdated(statuses);
    }

    if (actions.mFailed) {
        return ProcessFailed(statuses, ReadFailedReason());
    }

    if (actions.mDoUpdate) {
        return ProcessWaitReboot(statuses, InstanceStateEnum::eActivating, actions);
    }

    return ProcessNoAction(statuses);
}

// No valid pending instance: update can't be tracked, so it should be canceled if it is safe.
Error RootfsRuntime::ProcessNoPending(Array<InstanceStatus>& statuses, const Actions& actions)
{
    if (actions.mDoApply) {
        // do_apply means that the update is confirmed or initramfs failed to apply it. In the latter case the rootfs
        // may be partially updated, and initramfs revert only clears action files without restoring it. Cancellation is
        // not safe: artifacts are kept, so initramfs retries the apply on next boot, and recovery required is reported.
        LOG_ERR() << "Rootfs update state is unknown, update artifacts are kept";

        mCurrentError
            = AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "rootfs update state is unknown, recovery is required"));
        mApplyIncomplete = true;
    } else if (actions.mUpdated) {
        // Trial boot: initramfs didn't modify the rootfs, so the update can be safely reverted.
        LOG_ERR() << "Rootfs trial boot without valid pending instance, revert update";

        if (auto err = RemoveFile(GetPath(cPendingInstanceFileName)); !err.IsNone()) {
            LOG_ERR() << "Can't remove pending instance" << Log::Field(AOS_ERROR_WRAP(err));
        }

        if (auto err = StoreAction(ActionTypeEnum::eFailed, "pending instance info is missing or corrupted");
            !err.IsNone()) {
            LOG_ERR() << "Can't store failed action" << Log::Field(err);
        }

        mRebootRequired = true;
    } else {
        if (auto err = ClearUpdateArtifacts(); !err.IsNone()) {
            LOG_ERR() << "Failed to clear update artifacts" << Log::Field(err);
        }

        // No update in progress: the installed rootfs is running.
        if (auto err = VerifyInstalledInstance(statuses); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    if (auto err = statuses.EmplaceBack(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    FillInstanceStatus(mCurrentInstance,
        mCurrentError.IsNone() ? InstanceStateEnum::eActive : InstanceStateEnum::eFailed, statuses.Back());
    statuses.Back().mError = mCurrentError;

    return ErrorEnum::eNone;
}

Error RootfsRuntime::ProcessUpdated(Array<InstanceStatus>& statuses)
{
    auto [isPendingBoot, bootErr] = IsPendingBoot();
    if (!bootErr.IsNone()) {
        return AOS_ERROR_WRAP(bootErr);
    }

    // Pending boot ID is updated to the trial boot ID only when the verdict is being stored. Same boot means that SM
    // was restarted while the verdict was stored: confirm and reject start with the same step, so the verdict is
    // unknown. The update is rejected, otherwise the health check may confirm the update that it has just rejected.
    if (isPendingBoot) {
        return RejectTrialBoot(statuses, AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "update verdict is interrupted")));
    }

    if (auto err = VerifyRootfsVersion(mRootfsConfig.mVersionFilePath, mPendingInstance.mVersion); !err.IsNone()) {
        LOG_ERR() << "Rootfs trial boot verification failed" << Log::Field(err);

        return RejectTrialBoot(statuses, err);
    }

    if (auto err = statuses.EmplaceBack(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    FillInstanceStatus(mPendingInstance, InstanceStateEnum::eActivating, statuses.Back());

    // Health check is started by Start after statuses are sent.
    mRunHealthCheck = true;

    return ErrorEnum::eNone;
}

// Rejects the update during trial boot: initramfs reverts the update on next boot as do_apply is absent. Error is
// passed by value as it may refer to the pending error, which is overwritten.
Error RootfsRuntime::RejectTrialBoot(Array<InstanceStatus>& statuses, Error err)
{
    LOG_ERR() << "Rootfs trial boot is rejected" << Log::Field(err);

    if (auto storeErr = StoreVerdict(false, err); !storeErr.IsNone()) {
        LOG_ERR() << "Can't store rootfs update failure" << Log::Field(storeErr);
    }

    mPendingState = InstanceStateEnum::eFailed;
    mPendingError = err;

    if (auto emplaceErr = statuses.EmplaceBack(); !emplaceErr.IsNone()) {
        return AOS_ERROR_WRAP(emplaceErr);
    }

    FillInstanceStatus(mPendingInstance, mPendingState, statuses.Back());
    statuses.Back().mError = mPendingError;

    mRebootRequired = true;

    return ErrorEnum::eNone;
}

// The node runs the pending rootfs if the version file matches the pending version that differs from the installed one.
// Same version update can't be detected and is treated as not running.
bool RootfsRuntime::IsTrialRootfs() const
{
    if (mPendingInstance.mVersion == mCurrentInstance.mVersion) {
        return false;
    }

    return VerifyRootfsVersion(mRootfsConfig.mVersionFilePath, mPendingInstance.mVersion).IsNone();
}

// Rootfs may be changed without SM tracking, e.g. if pending instance is lost while do_apply exists. When the installed
// rootfs is running, the installed instance is checked against the version file and restored from it on mismatch, the
// previous one is reported as inactive. Restore errors don't fail the start: the check is repeated on next start.
Error RootfsRuntime::VerifyInstalledInstance(Array<InstanceStatus>& statuses)
{
    auto [version, err] = GetCurrentVersion(mRootfsConfig.mVersionFilePath);
    if (!err.IsNone()) {
        LOG_ERR() << "Can't verify installed instance" << Log::Field(AOS_ERROR_WRAP(err));

        return ErrorEnum::eNone;
    }

    if (version == mCurrentInstance.mVersion) {
        return ErrorEnum::eNone;
    }

    LOG_ERR() << "Installed instance doesn't match rootfs, restore default"
              << Log::Field("installed", mCurrentInstance.mVersion) << Log::Field("rootfs", version);

    auto prevInstance = std::make_unique<InstanceInfo>(mCurrentInstance);

    ResetInstanceInfo(mCurrentInstance);

    static_cast<InstanceIdent&>(mCurrentInstance) = mDefaultInstanceIdent;
    mCurrentInstance.mVersion                     = version;

    if (auto saveErr = SaveInstanceInfo(mCurrentInstance, GetPath(cInstalledInstanceFileName)); !saveErr.IsNone()) {
        LOG_ERR() << "Can't save installed instance" << Log::Field(saveErr);
    }

    if (static_cast<const InstanceIdent&>(*prevInstance) != static_cast<const InstanceIdent&>(mCurrentInstance)
        || prevInstance->mManifestDigest != mCurrentInstance.mManifestDigest) {
        if (auto emplaceErr = statuses.EmplaceBack(); !emplaceErr.IsNone()) {
            return AOS_ERROR_WRAP(emplaceErr);
        }

        FillInstanceStatus(*prevInstance, InstanceStateEnum::eInactive, statuses.Back());
    }

    return ErrorEnum::eNone;
}

// Update is waiting for reboot to be processed by initramfs. If SM is restarted within the same boot, reboot is
// requested again. If reboot was done but initramfs didn't process the update, the update is failed.
Error RootfsRuntime::ProcessWaitReboot(Array<InstanceStatus>& statuses, InstanceStateEnum state, const Actions& actions)
{
    auto [isPendingBoot, bootErr] = IsPendingBoot();
    if (!bootErr.IsNone()) {
        return AOS_ERROR_WRAP(bootErr);
    }

    if (!isPendingBoot && actions.mDoApply) {
        // Initramfs failed to apply the update, rootfs may be partially updated. Update artifacts are kept, so
        // initramfs retries to apply the update on next boot.
        auto err
            = mPendingError.IsNone() ? Error(ErrorEnum::eFailed, "update is not applied after reboot") : mPendingError;

        LOG_ERR() << "Rootfs update apply failed, apply will be retried on next boot" << Log::Field(err);

        mPendingState    = InstanceStateEnum::eFailed;
        mPendingError    = err;
        mRebootRequired  = false;
        mApplyIncomplete = true;

        if (auto emplaceErr = statuses.EmplaceBack(); !emplaceErr.IsNone()) {
            return AOS_ERROR_WRAP(emplaceErr);
        }

        FillInstanceStatus(mPendingInstance, mPendingState, statuses.Back());
        statuses.Back().mError = mPendingError;

        return ErrorEnum::eNone;
    }

    // updated with failed after reboot: initramfs revert removes updated, so initramfs started the trial boot again and
    // failed is stale. It happens if only part of the revert flags removal is durable. Clearing artifacts would leave
    // the node running the trial rootfs reported as the installed one: the update is rejected and reverted instead.
    // The version check prevents a reboot loop if initramfs didn't process the flags at all.
    if (!isPendingBoot && actions.mUpdated && IsTrialRootfs()) {
        return RejectTrialBoot(statuses, mPendingError);
    }

    if (!isPendingBoot) {
        auto err
            = actions.mFailed ? ReadFailedReason() : Error(ErrorEnum::eFailed, "update is not processed after reboot");

        return ProcessFailed(statuses, err);
    }

    LOG_INF() << "Rootfs update is waiting for reboot" << Log::Field("state", InstanceState(state));

    mPendingState = state;

    auto rebootAllowed = true;

    if (actions.mDoApply && state == InstanceStateEnum::eFailed) {
        // Update is rejected but do_apply removal failed: retry it, otherwise initramfs applies rejected update. Reboot
        // is not requested until do_apply is durably removed, removal is retried on next start.
        if (auto err
            = AOS_ERROR_WRAP(RemoveFileDurable(GetPath(ActionType(ActionTypeEnum::eDoApply).ToString().CStr())));
            !err.IsNone()) {
            LOG_ERR() << "Can't remove action, reboot is postponed" << Log::Field(err);

            rebootAllowed = false;
        }
    } else if (actions.mDoApply && !mPendingConfirmed) {
        // SM was interrupted after do_apply is stored but before update is confirmed. Directory is synced to make sure
        // do_apply is durable before update is confirmed.
        auto err = AOS_ERROR_WRAP(SyncDir(mRootfsConfig.mWorkingDir));

        if (err.IsNone()) {
            err = SaveInstanceInfo(mPendingInstance, GetPath(cPendingInstanceFileName), mPendingBootID, true);
        }

        if (!err.IsNone()) {
            LOG_ERR() << "Can't confirm update" << Log::Field(err);
        } else {
            mPendingConfirmed = true;
        }
    }

    if (auto err = statuses.EmplaceBack(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    FillInstanceStatus(mPendingInstance, mPendingState, statuses.Back());
    statuses.Back().mError = mPendingError;

    mRebootRequired = rebootAllowed;

    return ErrorEnum::eNone;
}

Error RootfsRuntime::ProcessFailed(Array<InstanceStatus>& statuses, const Error& err)
{
    LOG_ERR() << "Rootfs update failed" << Log::Field("ident", static_cast<const InstanceIdent&>(mPendingInstance))
              << Log::Field("version", mPendingInstance.mVersion) << Log::Field(err);

    mPendingState   = InstanceStateEnum::eFailed;
    mPendingError   = err;
    mRebootRequired = false;

    if (auto emplaceErr = statuses.EmplaceBack(); !emplaceErr.IsNone()) {
        return AOS_ERROR_WRAP(emplaceErr);
    }

    FillInstanceStatus(mPendingInstance, mPendingState, statuses.Back());
    statuses.Back().mError = mPendingError;

    // Pending instance is kept in memory to report failed status on start instance request.
    if (auto clearErr = ClearUpdateArtifacts(); !clearErr.IsNone()) {
        LOG_ERR() << "Failed to clear update artifacts" << Log::Field(clearErr);
    }

    mHasPending = false;

    // Update failed or reverted without do_apply: the installed rootfs is running.
    if (auto verifyErr = VerifyInstalledInstance(statuses); !verifyErr.IsNone()) {
        return AOS_ERROR_WRAP(verifyErr);
    }

    if (auto emplaceErr = statuses.EmplaceBack(); !emplaceErr.IsNone()) {
        return AOS_ERROR_WRAP(emplaceErr);
    }

    FillInstanceStatus(mCurrentInstance, InstanceStateEnum::eActive, statuses.Back());

    return ErrorEnum::eNone;
}

// No action files and pending instance: either the update is applied by initramfs or the update was interrupted before
// it was committed or confirmed. Update is promoted only if it is confirmed and rootfs version matches.
Error RootfsRuntime::ProcessNoAction(Array<InstanceStatus>& statuses)
{
    // Not confirmed update with different version is promoted if rootfs version matches: version change proves that the
    // update is applied. Same version update requires confirmation.
    if (!mPendingConfirmed && mPendingInstance.mVersion == mCurrentInstance.mVersion) {
        return ProcessFailed(statuses, AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "update is not confirmed")));
    }

    if (auto err = VerifyRootfsVersion(mRootfsConfig.mVersionFilePath, mPendingInstance.mVersion); !err.IsNone()) {
        return ProcessFailed(statuses, err);
    }

    LOG_INF() << "Rootfs update applied" << Log::Field("ident", static_cast<const InstanceIdent&>(mPendingInstance))
              << Log::Field("version", mPendingInstance.mVersion);

    if (auto err = SaveInstanceInfo(mPendingInstance, GetPath(cInstalledInstanceFileName)); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    auto prevInstance = std::make_unique<InstanceInfo>(mCurrentInstance);

    mCurrentInstance = mPendingInstance;

    ResetPending();

    // If cleanup is interrupted, the same update is promoted again on next start.
    if (auto err = ClearUpdateArtifacts(); !err.IsNone()) {
        LOG_ERR() << "Failed to clear update artifacts" << Log::Field(err);
    }

    // Previous instance may be equal to current one if promotion was interrupted before pending instance removal.
    if (static_cast<const InstanceIdent&>(*prevInstance) != static_cast<const InstanceIdent&>(mCurrentInstance)
        || prevInstance->mManifestDigest != mCurrentInstance.mManifestDigest) {
        if (auto err = statuses.EmplaceBack(); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        FillInstanceStatus(*prevInstance, InstanceStateEnum::eInactive, statuses.Back());
    }

    if (auto err = statuses.EmplaceBack(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    FillInstanceStatus(mCurrentInstance, InstanceStateEnum::eActive, statuses.Back());

    return ErrorEnum::eNone;
}

// Stores health check verdict. Pending instance boot ID is updated first to detect SM restart before reboot. Confirmed
// flag is stored only after do_apply is durable, so confirmed pending instance without action files means that
// initramfs has applied the update. Any interruption between these steps results in failed update, never in promotion
// of not applied update.
Error RootfsRuntime::StoreVerdict(bool confirmed, const Error& err)
{
    auto [bootID, bootErr] = GetBootID();
    if (!bootErr.IsNone()) {
        return AOS_ERROR_WRAP(bootErr);
    }

    const auto pendingPath = GetPath(cPendingInstanceFileName);

    if (auto saveErr = SaveInstanceInfo(mPendingInstance, pendingPath, bootID); !saveErr.IsNone()) {
        return saveErr;
    }

    mPendingBootID    = bootID;
    mPendingConfirmed = false;

    if (!confirmed) {
        if (auto removeErr
            = AOS_ERROR_WRAP(RemoveFileDurable(GetPath(ActionType(ActionTypeEnum::eDoApply).ToString().CStr())));
            !removeErr.IsNone()) {
            return removeErr;
        }

        return StoreAction(ActionTypeEnum::eFailed, ErrorToString(err));
    }

    if (auto storeErr = StoreAction(ActionTypeEnum::eDoApply); !storeErr.IsNone()) {
        return storeErr;
    }

    if (auto saveErr = SaveInstanceInfo(mPendingInstance, pendingPath, bootID, true); !saveErr.IsNone()) {
        return saveErr;
    }

    mPendingConfirmed = true;

    return ErrorEnum::eNone;
}

void RootfsRuntime::ResetPending()
{
    mHasPending = false;
    ResetInstanceInfo(mPendingInstance);
    mPendingState = InstanceStateEnum::eActivating;
    mPendingError = ErrorEnum::eNone;
    mPendingBootID.clear();
    mPendingConfirmed = false;
    mRebootRequired   = false;
}

// Fills status if the instance is the current or the pending one. Returns false if the instance is unknown and the
// update should be prepared. Returns the update error if the pending instance is failed: the launcher treats successful
// start as active instance.
RetWithError<bool> RootfsRuntime::GetCurrentOrPendingStatus(const InstanceInfo& instance, InstanceStatus& status) const
{
    if (static_cast<const InstanceIdent&>(instance) == static_cast<const InstanceIdent&>(mCurrentInstance)
        && mCurrentInstance.mManifestDigest == instance.mManifestDigest) {
        // Current instance is failed if the update state is unknown and recovery is required.
        if (!mCurrentError.IsNone()) {
            FillInstanceStatus(mCurrentInstance, InstanceStateEnum::eFailed, status);
            status.mError = mCurrentError;

            return {true, mCurrentError};
        }

        FillInstanceStatus(mCurrentInstance, InstanceStateEnum::eActive, status);

        return true;
    }

    if (mPendingInstance.mItemID.IsEmpty()
        || static_cast<const InstanceIdent&>(instance) != static_cast<const InstanceIdent&>(mPendingInstance)
        || mPendingInstance.mManifestDigest != instance.mManifestDigest) {
        return false;
    }

    FillInstanceStatus(mPendingInstance, mPendingState, status);
    status.mError = mPendingError;

    if (mPendingState != InstanceStateEnum::eFailed) {
        return true;
    }

    return {true, mPendingError.IsNone() ? AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "update failed")) : mPendingError};
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

RetWithError<bool> RootfsRuntime::IsPendingBoot() const
{
    auto [bootID, err] = GetBootID();
    if (!err.IsNone()) {
        return {false, err};
    }

    return bootID == mPendingBootID;
}

void RootfsRuntime::FillInstanceStatus(
    const InstanceInfo& instanceInfo, InstanceStateEnum state, InstanceStatus& status) const
{
    static_cast<InstanceIdent&>(status) = static_cast<const InstanceIdent&>(instanceInfo);
    status.mState                       = state;
    status.mVersion                     = instanceInfo.mVersion;
    status.mRuntimeID                   = mRuntimeInfo.mRuntimeID;
    status.mManifestDigest              = instanceInfo.mManifestDigest;
    status.mType                        = UpdateItemTypeEnum::eComponent;
    status.mPreinstalled                = instanceInfo.mPreinstalled;
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

// Image is copied to temporary file, verified against manifest layer descriptor and atomically renamed. Temporary file
// extension doesn't match initramfs image search pattern, so partially copied image is never used for update.
Error RootfsRuntime::CopyImage(const oci::ImageManifest& manifest) const
{
    const auto& layer = manifest.mLayers[0];

    std::string hash;

    if (auto err = ParseSHA256Digest(layer.mDigest, hash); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    StaticString<cFilePathLen> imageArchivePath;

    if (auto err = mItemInfoProvider->GetBlobPath(layer.mDigest, imageArchivePath); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    LOG_DBG() << "Copy image layer" << Log::Field("digest", layer.mDigest)
              << Log::Field("path", imageArchivePath.CStr());

    if (auto err = CopyFileDurable(imageArchivePath.CStr(), GetPath(cImageFileName), layer.mSize, hash);
        !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

// Cleanup is done in steps, each step is synced and cleanup stops if the step fails:
// - do_apply: initramfs applies the update if do_apply exists. If pending instance is removed first, the update applied
//   on next boot is not tracked and the installed instance doesn't match the rootfs. Without do_apply, initramfs
//   reverts the trial boot, and the remaining pending instance is reported as failed;
// - pending instance: action files without pending instance are treated as stale and removed on start, while removing
//   action files (e.g. failed) when pending instance remains may lead to promotion of failed update;
// - other action files: removing image while action files remain may lead to update without image.
// Image and temporary files removal errors don't stop cleanup, the first error is returned.
Error RootfsRuntime::ClearUpdateArtifacts() const
{
    // do_apply must never remain without do_update and image: initramfs treats missing do_update as full update, and
    // applying incremental image as full one removes the rootfs files missing in the image.
    if (auto err = AOS_ERROR_WRAP(RemoveFileDurable(GetPath(ActionType(ActionTypeEnum::eDoApply).ToString().CStr())));
        !err.IsNone()) {
        LOG_ERR() << "Failed to remove action file" << Log::Field(err);

        return err;
    }

    if (auto err = AOS_ERROR_WRAP(RemoveFileDurable(GetPath(cPendingInstanceFileName))); !err.IsNone()) {
        LOG_ERR() << "Failed to remove pending instance" << Log::Field(err);

        return err;
    }

    Error firstErr;

    auto handleErr = [&firstErr](const Error& err, const char* msg) {
        if (err.IsNone()) {
            return;
        }

        LOG_ERR() << msg << Log::Field(err);

        if (firstErr.IsNone()) {
            firstErr = err;
        }
    };

    for (const auto action : {ActionTypeEnum::eUpdated, ActionTypeEnum::eDoUpdate, ActionTypeEnum::eFailed}) {
        if (auto err = AOS_ERROR_WRAP(RemoveFile(GetPath(ActionType(action).ToString().CStr()))); !err.IsNone()) {
            LOG_ERR() << "Failed to remove action file" << Log::Field(err);

            return err;
        }
    }

    // Action files removal should be durable before image is removed: otherwise initramfs may find action files without
    // image.
    if (auto err = AOS_ERROR_WRAP(SyncDir(mRootfsConfig.mWorkingDir)); !err.IsNone()) {
        LOG_ERR() << "Failed to sync working dir" << Log::Field(err);

        return err;
    }

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

Error RootfsRuntime::StoreAction(const ActionType& action, std::string_view data) const
{
    if (auto err = WriteFileDurable(GetPath(action.ToString().CStr()), data); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

// Lookup errors are returned instead of treating the action as absent: wrong update state may lead to promotion of
// unconfirmed update.
Error RootfsRuntime::ReadActions(Actions& actions) const
{
    auto hasAction = [this](ActionTypeEnum action, bool& exists) {
        std::error_code ec;

        exists = std::filesystem::exists(GetPath(ActionType(action).ToString().CStr()), ec);
        if (ec) {
            return Error(ec.value(), "can't check action file");
        }

        return Error(ErrorEnum::eNone);
    };

    for (const auto& [action, exists] : {std::pair {ActionTypeEnum::eUpdated, &actions.mUpdated},
             std::pair {ActionTypeEnum::eDoApply, &actions.mDoApply},
             std::pair {ActionTypeEnum::eDoUpdate, &actions.mDoUpdate},
             std::pair {ActionTypeEnum::eFailed, &actions.mFailed}}) {
        if (auto err = hasAction(action, *exists); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    return ErrorEnum::eNone;
}

Error RootfsRuntime::ReadFailedReason() const
{
    std::ifstream file(GetPath(ActionType(ActionTypeEnum::eFailed).ToString().CStr()));
    std::string   reason;

    if (file.is_open()) {
        std::stringstream buffer;

        buffer << file.rdbuf();
        reason = Poco::trim(buffer.str());
    }

    if (reason.empty()) {
        reason = "update failed";
    }

    return Error(ErrorEnum::eFailed, reason.c_str());
}

// Partially updated rootfs (initramfs apply failed or update state is unknown) can be restored only by full update:
// incremental image is mounted over and applied to the current rootfs. Incremental update is refused without changing
// update artifacts, so initramfs retries the apply on next boot. Any other preparation error clears update artifacts.
Error RootfsRuntime::PrepareUpdate(const InstanceInfo& instance)
{
    LOG_DBG() << "Preparing update" << Log::Field("ident", static_cast<const InstanceIdent&>(instance));

    auto imageManifest = std::make_unique<oci::ImageManifest>();
    auto err           = GetImageManifest(instance.mManifestDigest, *imageManifest);
    auto updateType    = std::string();

    if (err.IsNone()) {
        Tie(updateType, err) = GetUpdateType(*imageManifest);
    }

    if (err.IsNone() && mApplyIncomplete && updateType != cFullUpdateType) {
        LOG_ERR() << "Rootfs may be partially updated, incremental update is refused";

        return AOS_ERROR_WRAP(
            Error(ErrorEnum::eWrongState, "rootfs may be partially updated, full update is required"));
    }

    if (err.IsNone()) {
        err = StoreUpdate(instance, *imageManifest, updateType);
    }

    if (!err.IsNone()) {
        if (auto clearErr = ClearUpdateArtifacts(); !clearErr.IsNone()) {
            LOG_ERR() << "Failed to clear update artifacts" << Log::Field(clearErr);
        }

        // Memory state is reset even if cleanup failed: remaining artifacts are handled on next start according to the
        // cleanup order.
        ResetPending();

        return err;
    }

    return ErrorEnum::eNone;
}

// Update artifacts are written in order: image, pending instance, do_update. do_update is the commit point: initramfs
// starts the update only if it exists, and at this moment all other artifacts are durable.
Error RootfsRuntime::StoreUpdate(
    const InstanceInfo& instance, const oci::ImageManifest& manifest, const std::string& updateType)
{
    auto [bootID, bootErr] = GetBootID();
    if (!bootErr.IsNone()) {
        return AOS_ERROR_WRAP(bootErr);
    }

    // Previous do_update commit marker must be durably removed before update artifacts are replaced.
    if (auto err = ClearUpdateArtifacts(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    ResetPending();

    if (auto err = CopyImage(manifest); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = SaveInstanceInfo(instance, GetPath(cPendingInstanceFileName), bootID); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = StoreAction(ActionTypeEnum::eDoUpdate, updateType); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    mHasPending      = true;
    mPendingInstance = instance;
    mPendingBootID   = bootID;
    mRebootRequired  = true;

    return ErrorEnum::eNone;
}

std::filesystem::path RootfsRuntime::GetPath(const std::string& fileName) const
{
    return std::filesystem::path(mRootfsConfig.mWorkingDir) / fileName;
}

} // namespace aos::sm::launcher
