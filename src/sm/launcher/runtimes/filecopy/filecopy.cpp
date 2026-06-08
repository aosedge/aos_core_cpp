/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>

#include <core/common/ocispec/itf/imagespec.hpp>
#include <core/common/tools/fs.hpp>
#include <core/common/tools/logger.hpp>

#include <common/utils/exception.hpp>
#include <common/utils/json.hpp>
#include <common/utils/utils.hpp>

#include <sm/launcher/runtimes/utils/utils.hpp>

#include "config.hpp"
#include "filecopy.hpp"

namespace aos::sm::launcher {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error FileCopyRuntime::Init(const RuntimeConfig& config, iamclient::CurrentNodeInfoProviderItf& currentNodeInfoProvider,
    imagemanager::ItemInfoProviderItf& itemInfoProvider, oci::OCISpecItf& ociSpec,
    InstanceStatusReceiverItf& statusReceiver, sm::utils::SystemdConnItf& systemdConn)
{
    LOG_DBG() << "Init runtime" << Log::Field("type", config.mType.c_str());

    mRuntimeConfig           = config;
    mCurrentNodeInfoProvider = &currentNodeInfoProvider;
    mItemInfoProvider        = &itemInfoProvider;
    mOCISpec                 = &ociSpec;
    mStatusReceiver          = &statusReceiver;

    if (auto err = ParseConfig(mRuntimeConfig, mComponentConfig); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = fs::MakeDirAll(mComponentConfig.mRuntimeDir.c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = CreateRuntimeInfo(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = mRebooter.Init(systemdConn); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error FileCopyRuntime::Start()
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Start runtime";

    mCurrentInstance.reset();

    if (auto err = InitInstalledData(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (!mCurrentInstance.has_value()) {
        return ErrorEnum::eNone;
    }

    auto status = std::make_unique<InstanceStatus>();

    FillInstanceStatus(*mCurrentInstance, InstanceStateEnum::eActive, *status);

    mStatusReceiver->OnInstancesStatusesReceived(Array<InstanceStatus> {status.get(), 1});

    return ErrorEnum::eNone;
}

Error FileCopyRuntime::Stop()
{
    LOG_DBG() << "Stop runtime";

    return ErrorEnum::eNone;
}

Error FileCopyRuntime::GetRuntimeInfo(RuntimeInfo& runtimeInfo) const
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Get runtime info";

    runtimeInfo = mRuntimeInfo;

    return ErrorEnum::eNone;
}

Error FileCopyRuntime::StartInstance(const InstanceInfo& instance, InstanceStatus& status)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Start instance" << Log::Field("ident", static_cast<const InstanceIdent&>(instance))
              << Log::Field("version", instance.mVersion) << Log::Field("manifestDigest", instance.mManifestDigest);

    auto notify = DeferRelease(&status, [&](const InstanceStatus*) {
        mStatusReceiver->OnInstancesStatusesReceived(Array<InstanceStatus> {&status, 1});
    });

    if (mCurrentInstance.has_value() && mCurrentInstance->mManifestDigest == instance.mManifestDigest) {
        FillInstanceStatus(*mCurrentInstance, InstanceStateEnum::eActive, status);

        return ErrorEnum::eNone;
    }

    FillInstanceStatus(instance, InstanceStateEnum::eActivating, status);

    mStatusReceiver->OnInstancesStatusesReceived(Array<InstanceStatus> {&status, 1});

    Error err = ErrorEnum::eNone;

    auto cleanup = DeferRelease(&err, [&](const Error* e) {
        if (!e->IsNone()) {
            status.mState = InstanceStateEnum::eFailed;
            status.mError = *e;
        }
    });

    auto imageManifest = std::make_unique<oci::ImageManifest>();

    err = GetImageManifest(instance.mManifestDigest, *imageManifest);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    err = CopyImage(*imageManifest);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    err = SaveInstanceInfo(instance);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    mCurrentInstance = instance;

    err = mStatusReceiver->RebootRequired(mRuntimeInfo.mRuntimeID);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error FileCopyRuntime::StopInstance(const InstanceIdent& instance, InstanceStatus& status)
{
    LOG_DBG() << "Stop instance" << Log::Field("ident", instance);

    static_cast<InstanceIdent&>(status) = instance;
    status.mState                       = InstanceStateEnum::eInactive;
    status.mError                       = ErrorEnum::eNone;

    mStatusReceiver->OnInstancesStatusesReceived(Array<InstanceStatus> {&status, 1});

    return ErrorEnum::eNone;
}

Error FileCopyRuntime::Reboot()
{
    LOG_DBG() << "Reboot runtime";

    return mRebooter.Reboot();
}

Error FileCopyRuntime::GetInstanceMonitoringData(
    const InstanceIdent& instanceIdent, monitoring::InstanceMonitoringData& monitoringData)
{
    (void)monitoringData;

    LOG_DBG() << "Get instance monitoring data" << Log::Field("instance", instanceIdent);

    return ErrorEnum::eNotSupported;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

Error FileCopyRuntime::InitInstalledData()
{
    const auto path = std::filesystem::path(mComponentConfig.mRuntimeDir) / cInstalledInstanceFileName;

    if (!std::filesystem::exists(path)) {
        mCurrentInstance.emplace();
        static_cast<InstanceIdent&>(*mCurrentInstance) = mDefaultInstanceIdent;

        if (auto err = mCurrentInstance->mVersion.Assign(cDefaultVersion); !err.IsNone()) {
            mCurrentInstance.reset();

            return AOS_ERROR_WRAP(err);
        }

        if (auto err = SaveInstanceInfo(*mCurrentInstance); !err.IsNone()) {
            mCurrentInstance.reset();

            return AOS_ERROR_WRAP(err);
        }

        return ErrorEnum::eNone;
    }

    mCurrentInstance.emplace();

    if (auto err = LoadInstanceInfo(*mCurrentInstance); !err.IsNone()) {
        mCurrentInstance.reset();

        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error FileCopyRuntime::CreateRuntimeInfo()
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

void FileCopyRuntime::FillInstanceStatus(
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

Error FileCopyRuntime::SaveInstanceInfo(const InstanceInfo& instance) const
{
    const auto path = std::filesystem::path(mComponentConfig.mRuntimeDir) / cInstalledInstanceFileName;

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
        json->set("instance", instance.mInstance);
        json->set("manifestDigest", instance.mManifestDigest.CStr());
        json->set("version", instance.mVersion.CStr());
        json->set("preinstalled", instance.mPreinstalled);

        json->stringify(file);
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error FileCopyRuntime::LoadInstanceInfo(InstanceInfo& instance)
{
    const auto path = std::filesystem::path(mComponentConfig.mRuntimeDir) / cInstalledInstanceFileName;

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

        instance.mPreinstalled = jsonObject.GetValue<bool>("preinstalled");
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error FileCopyRuntime::GetImageManifest(const String& digest, oci::ImageManifest& manifest) const
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

Error FileCopyRuntime::CopyImage(const oci::ImageManifest& manifest) const
{
    if (manifest.mLayers.Size() == 0) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eInvalidArgument, "image manifest has no layers"));
    }

    const auto& layer = manifest.mLayers[0];

    StaticString<cFilePathLen> imageArchivePath;

    if (auto err = mItemInfoProvider->GetBlobPath(layer.mDigest, imageArchivePath); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    LOG_DBG() << "Install component image" << Log::Field("digest", layer.mDigest)
              << Log::Field("mediaType", layer.mMediaType) << Log::Field("src", imageArchivePath.CStr());

    if (auto err = fs::MakeDirAll(mComponentConfig.mTargetPath.c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (layer.mMediaType == oci::cMediaTypeComponentFullTarGZip) {
        if (auto err = fs::MakeDirAll(mComponentConfig.mTargetPath.c_str()); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (auto res
            = common::utils::ExecCommand({"tar", "-xzf", imageArchivePath.CStr(), "-C", mComponentConfig.mTargetPath});
            !res.mError.IsNone()) {
            return AOS_ERROR_WRAP(res.mError);
        }

        return ErrorEnum::eNone;
    }

    std::error_code ec;

    std::filesystem::copy_file(imageArchivePath.CStr(), mComponentConfig.mTargetPath.c_str(),
        std::filesystem::copy_options::overwrite_existing, ec);

    if (ec.value() != 0) {
        return AOS_ERROR_WRAP(Error(ec.value(), ec.message().c_str()));
    }

    return ErrorEnum::eNone;
}

} // namespace aos::sm::launcher
