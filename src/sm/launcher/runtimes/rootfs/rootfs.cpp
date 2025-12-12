/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>
#include <unordered_map>

#include <Poco/PipeStream.h>
#include <Poco/Process.h>
#include <Poco/StreamCopier.h>

#include <common/utils/exception.hpp>
#include <common/utils/json.hpp>
#include <common/utils/retry.hpp>
#include <core/common/tools/fs.hpp>
#include <core/common/tools/logger.hpp>

#include "rootfs.hpp"

namespace aos::sm::launcher::rootfs {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error RootfsRuntime::Init(const RootfsConfig& config, iamclient::CurrentNodeInfoProviderItf& currentNodeInfoProvider,
    ImageManagerItf& imageManager, oci::OCISpecItf& ociSpec, InstanceStatusReceiverItf& statusReceiver,
    UpdateCheckerItf& updateChecker, RebooterItf& rebooter)
{
    LOG_DBG() << "Initialize rootfs runtime";

    mConfig                  = config;
    mCurrentNodeInfoProvider = &currentNodeInfoProvider;
    mImageManager            = &imageManager;
    mOCISpec                 = &ociSpec;
    mStatusReceiver          = &statusReceiver;
    mUpdateChecker           = &updateChecker;
    mRebooter                = &rebooter;

    return ErrorEnum::eNone;
}

Error RootfsRuntime::Start()
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Starting rootfs runtime" << Log::Field("version", mCurrentVersion);

    InstanceStatus status;

    if (auto err = ProcessUpdateAction(status); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = InitCurrentData(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = SetRuntimeID(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    FillInstanceStatus(status);

    mStatusReceiver->OnInstancesStatusesReceived(Array<InstanceStatus> {&status, 1});

    return ErrorEnum::eNone;
}

Error RootfsRuntime::Stop()
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Stopping rootfs runtime";

    return ErrorEnum::eNone;
}

Error RootfsRuntime::GetRuntimeInfo(RuntimeInfo& runtimeInfo) const
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Getting rootfs runtime info";

    runtimeInfo = static_cast<const RuntimeInfo&>(mConfig);

    return ErrorEnum::eNone;
}

Error RootfsRuntime::StartInstance(const InstanceInfo& instance, InstanceStatus& status)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Starting instance" << Log::Field("ident", static_cast<const InstanceIdent&>(instance))
              << Log::Field("manifestDigest", instance.mManifestDigest);

    if (mCurrentInstance == instance) {
        status.mState   = InstanceStateEnum::eActive;
        status.mError   = ErrorEnum::eNone;
        status.mVersion = mCurrentVersion;

        return ErrorEnum::eNone;
    }

    status.mState = InstanceStateEnum::eActivating;

    Error err = ErrorEnum::eNone;

    auto cleanup = DeferRelease(&err, [&](Error* err) {
        if (err->IsNone()) {
            status.mState = InstanceStateEnum::eActivating;

            return;
        }

        status.mState = InstanceStateEnum::eFailed;
        status.mError = *err;
    });

    err = PrepareUpdate(instance);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    err = mStatusReceiver->RebootRequired(mConfig.mRuntimeID);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error RootfsRuntime::StopInstance(const InstanceIdent& instance, InstanceStatus& status)
{
    LOG_DBG() << "Stopping instance" << Log::Field("ident", instance);

    status.mState = InstanceStateEnum::eInactive;
    status.mError = ErrorEnum::eNone;

    return ErrorEnum::eNone;
}

Error RootfsRuntime::Reboot()
{
    LOG_DBG() << "Rebooting rootfs runtime";

    return mRebooter->Reboot();
}

Error RootfsRuntime::GetInstanceMonitoringData(
    const InstanceIdent& instanceIdent, monitoring::InstanceMonitoringData& monitoringData)
{
    (void)monitoringData;

    LOG_DBG() << "Getting monitoring data for instance" << Log::Field("ident", instanceIdent);

    return ErrorEnum::eNotSupported;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

Error RootfsRuntime::InitCurrentData()
{
    if (auto err = LoadInstanceInfo(mConfig.mCurrentInstanceFile, mCurrentInstance);
        !err.IsNone() && !err.Is(ErrorEnum::eNotFound)) {
        return err;
    }

    if (auto err = fs::ReadFileToString(mConfig.mCurrentVersionFile.c_str(), mCurrentVersion); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error RootfsRuntime::SetRuntimeID()
{
    auto nodeInfo = std::make_unique<NodeInfo>();

    if (auto err = mCurrentNodeInfoProvider->GetCurrentNodeInfo(*nodeInfo); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    std::string runtimeID = std::string(mConfig.mRuntimeType.CStr()).append(":").append(nodeInfo->mNodeID.CStr());

    if (auto err = mConfig.mRuntimeID.Assign(runtimeID.c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error RootfsRuntime::ProcessUpdateAction(InstanceStatus& status)
{
    const auto action = ReadAction();

    LOG_DBG() << "Processing rootfs update action" << Log::Field("action", action.ToString().CStr());

    switch (action.GetValue()) {
    case ActionTypeEnum::eDoUpdate: {
        return ProcessDoUpdate(status);
    }

    case ActionTypeEnum::eUpdated: {
        return ProcessUpdated(status);
    }

    case ActionTypeEnum::eFailed: {
        return ProcessFailed(status);
    }

    default:
        break;
    }

    return ErrorEnum::eNone;
}

Error RootfsRuntime::ProcessDoUpdate(InstanceStatus& status)
{
    ActionType nextAction = ActionTypeEnum::eDoApply;

    if (auto err = mUpdateChecker->Check(); !err.IsNone()) {
        status.mState = InstanceStateEnum::eFailed;
        status.mError = AOS_ERROR_WRAP(err);

        nextAction = ActionTypeEnum::eFailed;
    }

    if (auto err = StoreAction(nextAction); !err.IsNone()) {
        status.mState = InstanceStateEnum::eFailed;
        status.mError = AOS_ERROR_WRAP(err);

        return ErrorEnum::eNone;
    }

    if (auto err = mRebooter->Reboot(); !err.IsNone()) {
        status.mState = InstanceStateEnum::eFailed;
        status.mError = AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error RootfsRuntime::ProcessUpdated(InstanceStatus& status)
{
    status.mState = InstanceStateEnum::eActive;

    std::error_code ec;

    std::filesystem::rename(mConfig.mUpdateInstanceFile, mConfig.mCurrentInstanceFile, ec);

    ClearUpdateArtifacts();

    if (ec) {
        status.mState = InstanceStateEnum::eFailed;
        status.mError = AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, ec.message().c_str()));
    }

    return ErrorEnum::eNone;
}

Error RootfsRuntime::ProcessFailed(InstanceStatus& status)
{
    status.mState = InstanceStateEnum::eFailed;

    ClearUpdateArtifacts();

    return ErrorEnum::eNone;
}

void RootfsRuntime::FillInstanceStatus(InstanceStatus& status) const
{
    static_cast<InstanceIdent&>(status) = mCurrentInstance;
    status.mVersion                     = mCurrentVersion;
    status.mRuntimeID                   = mConfig.mRuntimeID;
    status.mManifestDigest              = mCurrentInstance.mManifestDigest;
}

Error RootfsRuntime::SaveInstanceInfo(const InstanceInfo& instance, const std::string& path)
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

        json->stringify(file);
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error RootfsRuntime::LoadInstanceInfo(const std::string& path, InstanceInfo& instance)
{
    LOG_DBG() << "Load instance info" << Log::Field("path", path.c_str());

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
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error RootfsRuntime::GetImageManifest(const String& digest, oci::ImageManifest& manifest) const
{
    StaticString<cFilePathLen> blobPath;

    if (auto err = mImageManager->GetBlobPath(digest, blobPath); !err.IsNone()) {
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

    if (auto err = mImageManager->GetBlobPath(manifest.mLayers[0].mDigest, imageArchivePath); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    Poco::Process::Args args;
    args.push_back("xzf");
    args.push_back(imageArchivePath.CStr());
    args.push_back("-C");
    args.push_back(mConfig.mUpdateDir);

    Poco::Pipe          outPipe;
    Poco::ProcessHandle ph = Poco::Process::launch("tar", args, nullptr, &outPipe, &outPipe);
    int                 rc = ph.wait();

    if (rc != 0) {
        std::string           output;
        Poco::PipeInputStream istr(outPipe);
        Poco::StreamCopier::copyToString(istr, output);

        LOG_ERR() << output.c_str();

        return Error(ErrorEnum::eFailed, output.c_str());
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
    std::error_code ec;

    for (const auto& entry : std::filesystem::directory_iterator(mConfig.mUpdateDir)) {
        if (entry.path().extension() == cImageExtension) {
            std::filesystem::remove(entry.path(), ec);
            if (ec) {
                LOG_ERR() << "Failed to remove update artifact" << Log::Field("path", entry.path().c_str())
                          << Log::Field("error", ec.message().c_str());
            }
        }
    }

    if (auto err = StoreAction(cResetAllActionFiles); !err.IsNone()) {
        LOG_ERR() << "Failed to clear action files" << Log::Field(err);
    }
}

Error RootfsRuntime::StoreAction(ActionTypeEnum action, const std::string& data) const
{
    for (size_t i = 0; i < static_cast<size_t>(ActionTypeEnum::eNumActions); ++i) {
        const auto currentAction = ActionType(static_cast<ActionTypeEnum>(i));
        const auto path          = std::filesystem::path(mConfig.mUpdateDir) / currentAction.ToString().CStr();

        if (currentAction == action) {
            std::ofstream file(path);
            if (!file.is_open()) {
                return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "can't create action file"));
            }

            file << data;

            continue;
        }

        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    return ErrorEnum::eNone;
}

RootfsRuntime::ActionType RootfsRuntime::ReadAction() const
{
    for (size_t i = 0; i < static_cast<size_t>(ActionTypeEnum::eNumActions); ++i) {
        const auto currentAction = ActionType(static_cast<ActionTypeEnum>(i));
        const auto path          = std::filesystem::path(mConfig.mUpdateDir) / currentAction.ToString().CStr();

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

    if (auto err = SaveInstanceInfo(instance, mConfig.mUpdateInstanceFile); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::sm::launcher::rootfs
