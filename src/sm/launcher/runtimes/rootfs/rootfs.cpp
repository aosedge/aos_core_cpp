/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>
#include <unordered_map>

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

Error RootfsRuntime::Init(const Config& config, oci::OCISpecItf& ociSpec, InstanceStatusReceiverItf& statusReceiver,
    UpdateCheckerItf& updateChecker, RebooterItf& rebooter)
{
    LOG_DBG() << "Initialize rootfs runtime";

    mConfig         = config;
    mOCISpec        = &ociSpec;
    mStatusReceiver = &statusReceiver;
    mUpdateChecker  = &updateChecker;
    mRebooter       = &rebooter;

    if (auto err = InitCurrentData(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error RootfsRuntime::Start()
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Starting rootfs runtime";

    InstanceStatus status;

    static_cast<InstanceIdent&>(status) = mCurrentInstance;
    status.mVersion                     = mCurrentVersion;
    status.mRuntimeID                   = mConfig.mRuntimeInfo.mRuntimeID;
    status.mState                       = InstanceStateEnum::eActivating;

    switch (const auto action = GetCurrentAction(); action.GetValue()) {
    case ActionTypeEnum::eDoUpdate: {
        ActionType nextAction = ActionTypeEnum::eDoApply;

        if (auto err = mUpdateChecker->Check(); !err.IsNone()) {
            status.mState = InstanceStateEnum::eFailed;
            status.mError = AOS_ERROR_WRAP(err);

            nextAction = ActionTypeEnum::eFailed;
        }

        if (auto err = StoreAction(nextAction); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (auto err = mRebooter->Reboot(); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        break;
    }
    case ActionTypeEnum::eUpdated: {
        LOG_DBG() << "Rootfs runtime already updated";

        status.mState = InstanceStateEnum::eActive;

        ClearUpdateArtifacts();

        break;
    }
    case ActionTypeEnum::eFailed: {
        LOG_DBG() << "Previous rootfs runtime update failed";

        status.mState = InstanceStateEnum::eFailed;

        ClearUpdateArtifacts();

        break;
    }
    default:
        break;
    }

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

    runtimeInfo = mConfig.mRuntimeInfo;

    return ErrorEnum::eNone;
}

Error RootfsRuntime::StartInstance(const InstanceInfo& instance, InstanceStatus& status)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Starting instance" << Log::Field("ident", static_cast<const InstanceIdent&>(instance));

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
        }

        status.mState = InstanceStateEnum::eFailed;
        status.mError = *err;
    });

    err = PrepareUpdate(instance);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    err = mStatusReceiver->RebootRequired(mConfig.mRuntimeInfo.mRuntimeID);
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

Error RootfsRuntime::SaveInstanceInfo(const InstanceInfo& instance, const std::string& path)
{
    LOG_DBG() << "Save instance info" << Log::Field("ident", static_cast<const InstanceIdent&>(instance))
              << Log::Field("path", path.c_str());

    std::ofstream file(path);
    if (!file.is_open()) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "can't open version file for writing"));
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
        return AOS_ERROR_WRAP(Error(ErrorEnum::eNotFound, "can't open version file"));
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

Error RootfsRuntime::GetRootfsImagePath(const String& digest, String& path) const
{
    auto imageManifest = std::make_unique<oci::ImageManifest>();

    StaticString<cFilePathLen> manifestPath = digest; // TODO: get path by digest

    if (auto err = mOCISpec->LoadImageManifest(manifestPath, *imageManifest); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (imageManifest->mLayers.IsEmpty()) {
        return AOS_ERROR_WRAP(ErrorEnum::eNotFound);
    }

    path = imageManifest->mLayers[0].mDigest; // TODO: convert digest to path

    return ErrorEnum::eNone;
}

Error RootfsRuntime::CopyRootfsImage(const String& srcPath) const
{
    std::error_code ec;

    std::filesystem::path destination = mConfig.mUpdateDir;
    destination / std::filesystem::path(srcPath.CStr()).filename().append(cImageExtension);

    std::filesystem::copy_file(srcPath.CStr(), destination, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, ec.message().c_str()));
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

Error RootfsRuntime::StoreAction(ActionTypeEnum action) const
{
    for (size_t i = 0; i < static_cast<size_t>(ActionTypeEnum::eNumActions); ++i) {
        const auto currentAction = ActionType(static_cast<ActionTypeEnum>(i));
        const auto path          = std::filesystem::path(mConfig.mUpdateDir) / currentAction.ToString().CStr();

        if (currentAction == action) {
            std::ofstream file(path);
            if (!file.is_open()) {
                return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "can't create action file"));
            }

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

    if (auto err = GetRootfsImagePath(instance.mManifestDigest, rootfsImagePath); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = CopyRootfsImage(rootfsImagePath); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = StoreAction(ActionTypeEnum::eDoUpdate); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::sm::launcher::rootfs
