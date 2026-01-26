/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>

#include <Poco/String.h>
#include <Poco/StringTokenizer.h>

#include <core/common/tools/fs.hpp>
#include <core/common/tools/logger.hpp>

#include <common/utils/exception.hpp>
#include <common/utils/json.hpp>
#include <common/utils/utils.hpp>

#include "config.hpp"
#include "rootfs.hpp"

namespace aos::sm::launcher {

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

Error RootfsRuntime::Start()
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Start runtime";

    if (auto err = InitInstalledData(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = InitPendingData(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    auto statuses = std::make_unique<StaticArray<InstanceStatus, 2>>();

    if (auto err = ProcessUpdateAction(*statuses); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    mStatusReceiver->OnInstancesStatusesReceived(*statuses);

    return ErrorEnum::eNone;
}

Error RootfsRuntime::Stop()
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Stop runtime";

    if (mHealthCheckThread.has_value() && mHealthCheckThread->joinable()) {
        mHealthCheckThread->join();
    }

    mHealthCheckThread.reset();

    return ErrorEnum::eNone;
}

Error RootfsRuntime::GetRuntimeInfo(RuntimeInfo& runtimeInfo) const
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Get runtime info";

    runtimeInfo = mRuntimeInfo;

    return ErrorEnum::eNone;
}

Error RootfsRuntime::StartInstance(const InstanceInfo& instance, InstanceStatus& status)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Start instance" << Log::Field("ident", static_cast<const InstanceIdent&>(instance))
              << Log::Field("version", instance.mVersion) << Log::Field("manifestDigest", instance.mManifestDigest)
              << Log::Field("type", instance.mType);

    FillInstanceStatus(instance, InstanceStateEnum::eActivating, status);

    if (static_cast<const InstanceIdent&>(mCurrentInstance) == static_cast<const InstanceIdent&>(instance)
        && instance.mManifestDigest == mCurrentInstance.mManifestDigest) {
        status.mState = InstanceStateEnum::eActive;

        mStatusReceiver->OnInstancesStatusesReceived(Array<InstanceStatus> {&status, 1});

        return ErrorEnum::eNone;
    }

    mStatusReceiver->OnInstancesStatusesReceived(Array<InstanceStatus> {&status, 1});

    Error err = ErrorEnum::eNone;

    auto cleanup = DeferRelease(&err, [&](const Error* err) {
        if (!err->IsNone()) {
            ClearUpdateArtifacts();

            status.mState = InstanceStateEnum::eFailed;
            status.mError = *err;
        }

        mStatusReceiver->OnInstancesStatusesReceived(Array<InstanceStatus> {&status, 1});
    });

    err = PrepareUpdate(instance);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    err = mStatusReceiver->RebootRequired(mRuntimeInfo.mRuntimeID);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
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
    LOG_DBG() << "Start health check for rootfs update" << Log::Field("version", mPendingVersion);

    ActionType nextAction = ActionTypeEnum::eDoApply;

    if (auto err = mUpdateChecker.Check(); !err.IsNone()) {
        status->mState = InstanceStateEnum::eFailed;
        status->mError = AOS_ERROR_WRAP(err);

        nextAction = ActionTypeEnum::eFailed;
    }

    if (auto err = StoreAction(nextAction); !err.IsNone()) {
        status->mState = InstanceStateEnum::eFailed;
        status->mError = AOS_ERROR_WRAP(err);
    }

    if (auto err = mRebooter.Reboot(); !err.IsNone()) {
        status->mState = InstanceStateEnum::eFailed;
        status->mError = AOS_ERROR_WRAP(err);
    }

    mStatusReceiver->OnInstancesStatusesReceived(Array<InstanceStatus> {status.get(), 1});
}

RetWithError<StaticString<cVersionLen>> RootfsRuntime::GetCurrentVersion() const
{
    std::ifstream versionFile(mRootfsConfig.mVersionFilePath);
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

Error RootfsRuntime::InitInstalledData()
{
    const auto path = GetPath(cInstalledInstanceFileName);

    if (!std::filesystem::exists(path)) {
        auto [version, err] = GetCurrentVersion();
        if (!err.IsNone()) {
            return err;
        }

        static_cast<InstanceIdent&>(mCurrentInstance) = mDefaultInstanceIdent;
        mCurrentInstance.mVersion                     = version;

        err = SaveInstanceInfo(mCurrentInstance, path);
        if (!err.IsNone()) {
            return err;
        }
    }

    if (auto err = LoadInstanceInfo(path, mCurrentInstance); !err.IsNone()) {
        return err;
    }

    return ErrorEnum::eNone;
}

Error RootfsRuntime::InitPendingData()
{
    const auto path = GetPath(cPendingInstanceFileName);
    if (!std::filesystem::exists(path)) {
        return ErrorEnum::eNone;
    }

    if (auto err = LoadInstanceInfo(path, mPendingInstance); !err.IsNone()) {
        return err;
    }

    return ErrorEnum::eNone;
}

Error RootfsRuntime::CreateRuntimeInfo()
{
    auto nodeInfo = std::make_unique<NodeInfo>();

    if (auto err = mCurrentNodeInfoProvider->GetCurrentNodeInfo(*nodeInfo); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    auto runtimeID = mRuntimeConfig.mType + "-" + nodeInfo->mNodeID.CStr();

    if (auto err = mRuntimeInfo.mRuntimeID.Assign(common::utils::NameUUID(runtimeID).c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = mRuntimeInfo.mRuntimeType.Assign(mRuntimeConfig.mType.c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    mRuntimeInfo.mMaxInstances = 1;

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

Error RootfsRuntime::ProcessUpdateAction(Array<InstanceStatus>& statuses)
{
    const auto action = ReadAction();

    LOG_DBG() << "Process rootfs update action" << Log::Field("action", action.ToString().CStr());

    switch (action.GetValue()) {
    case ActionTypeEnum::eUpdated: {
        return ProcessUpdated(statuses);
    }

    case ActionTypeEnum::eFailed: {
        return ProcessFailed(statuses);
    }

    default: {
        return ProcessNoAction(statuses);
    }
    }
}

Error RootfsRuntime::ProcessUpdated(Array<InstanceStatus>& statuses)
{
    if (auto err = statuses.EmplaceBack(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    auto& status = statuses.Back();

    FillInstanceStatus(mPendingInstance, InstanceStateEnum::eActivating, status);

    mHealthCheckThread.emplace(&RootfsRuntime::RunHealthCheck, this, std::make_unique<InstanceStatus>(status));

    return ErrorEnum::eNone;
}

Error RootfsRuntime::ProcessFailed(Array<InstanceStatus>& statuses)
{
    if (auto err = statuses.EmplaceBack(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    FillInstanceStatus(mPendingInstance, InstanceStateEnum::eFailed, statuses.Back());

    ClearUpdateArtifacts();

    if (auto err = statuses.EmplaceBack(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    FillInstanceStatus(mCurrentInstance, InstanceStateEnum::eActive, statuses.Back());

    return ErrorEnum::eNone;
}

Error RootfsRuntime::ProcessNoAction(Array<InstanceStatus>& statuses)
{
    const auto pendingPath = std::filesystem::path(mRootfsConfig.mWorkingDir) / cPendingInstanceFileName;

    if (std::filesystem::exists(pendingPath)) {
        std::error_code ec;

        const auto installedPath = std::filesystem::path(mRootfsConfig.mWorkingDir) / cInstalledInstanceFileName;

        std::filesystem::rename(pendingPath, installedPath, ec);

        InitInstalledData();

        ClearUpdateArtifacts();

        if (ec) {
            statuses.Back().mState = InstanceStateEnum::eFailed;
            statuses.Back().mError = AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, ec.message().c_str()));
        }
    }

    if (auto err = statuses.EmplaceBack(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    FillInstanceStatus(mCurrentInstance, InstanceStateEnum::eActive, statuses.Back());

    return ErrorEnum::eNone;
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

Error RootfsRuntime::SaveInstanceInfo(const InstanceInfo& instance, const std::filesystem::path& path) const
{
    LOG_DBG() << "Save instance info" << Log::Field("ident", static_cast<const InstanceIdent&>(instance))
              << Log::Field("path", path.c_str());

    std::ofstream file(path);
    if (!file.is_open()) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "can't store instance info"));
    }

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    try {
        json->set("itemId", instance.mItemID.CStr());
        json->set("subjectId", instance.mSubjectID.CStr());
        json->set("manifestDigest", instance.mManifestDigest.CStr());
        json->set("type", instance.mType.ToString().CStr());
        json->set("version", instance.mVersion.CStr());
        json->set("preinstalled", instance.mPreinstalled);

        json->stringify(file);
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error RootfsRuntime::LoadInstanceInfo(const std::filesystem::path& path, InstanceInfo& instance)
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

        err = instance.mManifestDigest.Assign(jsonObject.GetValue<std::string>("manifestDigest").c_str());
        AOS_ERROR_CHECK_AND_THROW(err);

        err = instance.mVersion.Assign(jsonObject.GetValue<std::string>("version").c_str());
        AOS_ERROR_CHECK_AND_THROW(err);

        instance.mType         = UpdateItemTypeEnum::eComponent;
        instance.mPreinstalled = jsonObject.GetValue<bool>("preinstalled");
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
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

    return ErrorEnum::eNone;
}

Error RootfsRuntime::UnpackImage(const oci::ImageManifest& manifest) const
{
    if (manifest.mLayers.Size() == 0) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eInvalidArgument, "image manifest has no layers"));
    }

    StaticString<cFilePathLen> imageArchivePath;

    if (auto err = mItemInfoProvider->GetBlobPath(manifest.mLayers[0].mDigest, imageArchivePath); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    LOG_DBG() << "Unpack image layer" << Log::Field("digest", manifest.mLayers[0].mDigest)
              << Log::Field("path", imageArchivePath.CStr());

    const std::vector<std::string> cmdArgs {"tar", "xzf", imageArchivePath.CStr(), "-C", mRootfsConfig.mWorkingDir};

    auto [_, err] = common::utils::ExecCommand(cmdArgs);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error RootfsRuntime::PrepareUpdateFileContent(const oci::ImageManifest& manifest, std::string& updateType) const
{
    if (manifest.mLayers[0].mMediaType.FindSubstr(0, cFullMediaTypePrefix).mError.IsNone()) {
        updateType = "full";
    } else if (manifest.mLayers[0].mMediaType.FindSubstr(0, cIncrementalMediaTypePrefix).mError.IsNone()) {
        updateType = "incremental";
    } else {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eInvalidArgument, "unsupported artifact type"));
    }

    return ErrorEnum::eNone;
}

void RootfsRuntime::ClearUpdateArtifacts() const
{
    for (const auto& entry : std::filesystem::directory_iterator(mRootfsConfig.mWorkingDir)) {
        if (entry.path().extension() != cImageExtension && entry.path().filename() != cPendingInstanceFileName) {
            continue;
        }

        std::error_code ec;
        std::filesystem::remove(entry.path(), ec);
        if (ec) {
            LOG_ERR() << "Failed to remove update artifact" << Log::Field("path", entry.path().c_str())
                      << Log::Field("err", ec.message().c_str());
        }
    }

    for (size_t i = 0; i < static_cast<size_t>(ActionTypeEnum::eNumActions); ++i) {
        const auto currentAction = ActionType(static_cast<ActionTypeEnum>(i));
        const auto path          = std::filesystem::path(mRootfsConfig.mWorkingDir) / currentAction.ToString().CStr();

        std::error_code ec;
        std::filesystem::remove(path, ec);
        if (ec) {
            LOG_ERR() << "Failed to remove action file" << Log::Field("path", path.c_str())
                      << Log::Field("err", ec.message().c_str());
        }
    }
}

Error RootfsRuntime::StoreAction(const ActionType& action, const std::string& data) const
{
    const auto path = GetPath(action.ToString().CStr());

    std::ofstream file(path);
    if (!file.is_open()) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "can't create action file"));
    }

    file << data;

    return ErrorEnum::eNone;
}

RootfsRuntime::ActionType RootfsRuntime::ReadAction() const
{
    for (size_t i = 0; i < static_cast<size_t>(ActionTypeEnum::eNumActions); ++i) {
        const auto currentAction = ActionType(static_cast<ActionTypeEnum>(i));
        const auto path          = GetPath(currentAction.ToString().CStr());

        if (std::filesystem::exists(path)) {
            return currentAction;
        }
    }

    return ActionTypeEnum::eNumActions;
}

Error RootfsRuntime::PrepareUpdate(const InstanceInfo& instance)
{
    LOG_DBG() << "Preparing update" << Log::Field("ident", static_cast<const InstanceIdent&>(instance));

    StaticString<cFilePathLen> rootfsImagePath;

    auto imageManifest = std::make_unique<oci::ImageManifest>();

    if (auto err = GetImageManifest(instance.mManifestDigest, *imageManifest); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = UnpackImage(*imageManifest); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    std::string doUpdateConent;

    if (auto err = PrepareUpdateFileContent(*imageManifest, doUpdateConent); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = StoreAction(ActionTypeEnum::eDoUpdate, doUpdateConent); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = SaveInstanceInfo(instance, GetPath(cPendingInstanceFileName)); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

std::filesystem::path RootfsRuntime::GetPath(const std::string& fileName) const
{
    return std::filesystem::path(mRootfsConfig.mWorkingDir) / fileName;
}

} // namespace aos::sm::launcher
