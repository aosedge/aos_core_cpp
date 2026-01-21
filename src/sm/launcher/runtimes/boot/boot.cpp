/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <filesystem>
#include <fstream>
#include <regex>
#include <stdexcept>

#include <core/common/tools/logger.hpp>

#include <common/utils/exception.hpp>
#include <common/utils/filesystem.hpp>
#include <common/utils/utils.hpp>

#include "boot.hpp"
#include "eficontroller.hpp"
#include "partitionmanager.hpp"

namespace aos::sm::launcher {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error BootRuntime::Init(const RuntimeConfig& config, iamclient::CurrentNodeInfoProviderItf& currentNodeInfoProvider,
    imagemanager::ItemInfoProviderItf& itemInfoProvider, oci::OCISpecItf& ociSpec,
    InstanceStatusReceiverItf& statusReceiver, sm::utils::SystemdConnItf& systemdConn)
{
    LOG_DBG() << "Init runtime" << Log::Field("type", config.mType.c_str());

    mConfig                  = config;
    mCurrentNodeInfoProvider = &currentNodeInfoProvider;
    mItemInfoProvider        = &itemInfoProvider;
    mOCISpec                 = &ociSpec;
    mStatusReceiver          = &statusReceiver;

    if (auto err = ParseConfig(config, mBootConfig); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    mPartitionManager = CreatePartitionManager();
    if (!mPartitionManager) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "failed to create partition manager"));
    }

    mBootController = CreateBootController();
    if (!mBootController) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "failed to create boot controller"));
    }

    if (auto err = mBootController->Init(mBootConfig); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = mSystemdRebooter.Init(systemdConn); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = mSystemdUpdateChecker.Init(mBootConfig.mHealthCheckServices, systemdConn); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = mBootController->SetBootOK(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = InitBootPartitions(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = InitBootData(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = InitInstalledData(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = InitPendingData(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error BootRuntime::Start()
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Start runtime" << Log::Field("currentPartition", mCurrentPartition)
              << Log::Field("currentPartitionVersion", mCurrentPartitionVersion.c_str());

    auto nodeInfo = std::make_unique<NodeInfo>();

    if (auto err = mCurrentNodeInfoProvider->GetCurrentNodeInfo(*nodeInfo); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = CreateRuntimeInfo(mConfig.mType, *nodeInfo); !err.IsNone()) {
        return err;
    }

    auto instanceStatuses = std::make_unique<StaticArray<InstanceStatus, 2>>();

    if (auto err = HandleUpdate(*instanceStatuses); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = instanceStatuses->EmplaceBack(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    ToInstanceStatus(mInstalled, instanceStatuses->Back());

    if (auto err = mStatusReceiver->OnInstancesStatusesReceived(*instanceStatuses); !err.IsNone()) {
        LOG_WRN() << "Failed to send instances statuses" << Log::Field(AOS_ERROR_WRAP(err));
    }

    return ErrorEnum::eNone;
}

Error BootRuntime::Stop()
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Stop runtime";

    return ErrorEnum::eNone;
}

Error BootRuntime::GetRuntimeInfo(RuntimeInfo& runtimeInfo) const
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Get runtime info";

    runtimeInfo = mRuntimeInfo;

    return ErrorEnum::eNone;
}

Error BootRuntime::StartInstance(const InstanceInfo& instance, InstanceStatus& status)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Start instance" << Log::Field("instance", static_cast<const InstanceIdent&>(instance))
              << Log::Field("digest", instance.mManifestDigest);

    if (instance.mManifestDigest == mInstalled.mManifestDigest) {
        status.mState = mInstalled.mState;

        return ErrorEnum::eNone;
    }

    if (mPending.HasValue()) {
        LOG_DBG() << "Another update is already in progress"
                  << Log::Field("instance", static_cast<const InstanceIdent&>(*mPending))
                  << Log::Field("digest", mPending->mManifestDigest);

        return AOS_ERROR_WRAP(Error(ErrorEnum::eWrongState, "another update is already in progress"));
    }

    mPending.EmplaceValue();

    static_cast<InstanceIdent&>(*mPending) = static_cast<const InstanceIdent&>(instance);
    mPending->mManifestDigest              = instance.mManifestDigest;
    mPending->mState                       = InstanceStateEnum::eActivating;
    mPending->mPartitionIndex              = GetNextPartitionIndex(mInstalled.mPartitionIndex.GetValue());

    if (auto err = StoreData(cPendingInstance, *mPending); !err.IsNone()) {
        mPending->mError = AOS_ERROR_WRAP(err);
        mPending->mState = InstanceStateEnum::eFailed;
    }

    if (auto err = InstallPendingUpdate(); !err.IsNone()) {
        mPending->mError = AOS_ERROR_WRAP(err);
        mPending->mState = InstanceStateEnum::eFailed;
    }

    ToInstanceStatus(*mPending, status);

    return mPending->mError;
}

Error BootRuntime::StopInstance(const InstanceIdent& instance, InstanceStatus& status)
{
    std::lock_guard lock {mMutex};

    (void)status;

    LOG_DBG() << "Stop instance" << Log::Field("instance", instance);

    return ErrorEnum::eNone;
}

Error BootRuntime::Reboot()
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Reboot runtime";

    return mSystemdRebooter.Reboot();
}

Error BootRuntime::GetInstanceMonitoringData(
    const InstanceIdent& instanceIdent, monitoring::InstanceMonitoringData& monitoringData)
{
    std::lock_guard lock {mMutex};

    (void)monitoringData;

    LOG_DBG() << "Get instance monitoring data" << Log::Field("instance", instanceIdent);

    return ErrorEnum::eNotSupported;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

std::shared_ptr<PartitionManagerItf> BootRuntime::CreatePartitionManager() const
{
    return std::make_shared<PartitionManager>();
}

std::shared_ptr<BootControllerItf> BootRuntime::CreateBootController() const
{
    return std::make_shared<EFIBootController>();
}

Error BootRuntime::InitBootPartitions()
{
    if (auto err = mBootController->GetPartitionDevices(mPartitionDevices); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    for (const auto& device : mPartitionDevices) {
        LOG_DBG() << "Found partition device" << Log::Field("device", device.c_str());
    }

    if (mPartitionDevices.size() != cNumBootPartitions) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "unexpected number of boot partitions"));
    }

    return ErrorEnum::eNone;
}

Error BootRuntime::InitBootData()
{
    Error err = ErrorEnum::eNone;

    Tie(mCurrentPartition, err) = mBootController->GetCurrentBoot();
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    Tie(mMainPartition, err) = mBootController->GetMainBoot();
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    Tie(mCurrentPartitionVersion, err) = GetPartitionVersion(mCurrentPartition);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error BootRuntime::InitInstalledData()
{
    if (!std::filesystem::exists(GetPath(cInstalledInstance))) {
        mInstalled.mPartitionIndex.SetValue(mCurrentPartition);
        mInstalled.mVersion = mCurrentPartitionVersion.c_str();

        if (auto err = StoreData(cInstalledInstance, mInstalled); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    if (auto err = LoadData(cInstalledInstance, mInstalled); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error BootRuntime::InitPendingData()
{
    if (!std::filesystem::exists(GetPath(cPendingInstance))) {
        return ErrorEnum::eNone;
    }

    mPending.EmplaceValue();

    if (auto err = LoadData(cPendingInstance, *mPending); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (mPending->mPartitionIndex.HasValue() && mPending->mPartitionIndex.GetValue() == mCurrentPartition) {
        mPending->mVersion = mCurrentPartitionVersion.c_str();
    }

    return ErrorEnum::eNone;
}

Error BootRuntime::CreateRuntimeInfo(const std::string& runtimeType, const NodeInfo& nodeInfo)
{
    auto runtimeID = runtimeType + "-" + nodeInfo.mNodeID.CStr();

    if (auto err = mRuntimeInfo.mRuntimeID.Assign(common::utils::NameUUID(runtimeID).c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = mRuntimeInfo.mRuntimeType.Assign(runtimeType.c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    mRuntimeInfo.mMaxInstances = 1;

    LOG_INF() << "Runtime info" << Log::Field("runtimeID", mRuntimeInfo.mRuntimeID)
              << Log::Field("runtimeType", mRuntimeInfo.mRuntimeType)
              << Log::Field("maxInstances", mRuntimeInfo.mMaxInstances);

    return ErrorEnum::eNone;
}

Error BootRuntime::HandleUpdate(Array<InstanceStatus>& statuses)
{
    if (!mPending.HasValue()) {
        LOG_DBG() << "No pending updates";

        return ErrorEnum::eNone;
    }

    LOG_DBG() << "Handle update";

    statuses.EmplaceBack();

    if (mPending->mState == InstanceStateEnum::eFailed) {
        return HandleUpdateFailed(statuses.Back());
    }

    if (auto err = mSystemdUpdateChecker.Check(); !err.IsNone()) {
        mPending->mError = AOS_ERROR_WRAP(err);
        mPending->mState = InstanceStateEnum::eFailed;

        return HandleUpdateFailed(statuses.Back());
    }

    return HandleUpdateSucceeded(statuses.Back());
}

Error BootRuntime::HandleUpdateSucceeded(InstanceStatus& status)
{
    if (mCurrentPartition != *mPending->mPartitionIndex) {
        ToInstanceStatus(*mPending, status);

        if (auto err = mBootController->SetMainBoot(*mPending->mPartitionIndex); !err.IsNone()) {
            status.mError = AOS_ERROR_WRAP(err);
            status.mState = InstanceStateEnum::eFailed;

            return status.mError;
        }

        if (auto err = mStatusReceiver->RebootRequired(mRuntimeInfo.mRuntimeID); !err.IsNone()) {
            status.mError = AOS_ERROR_WRAP(err);
            status.mState = InstanceStateEnum::eFailed;

            return status.mError;
        }

        return ErrorEnum::eNone;
    }

    mInstalled.mState = InstanceStateEnum::eInactive;
    mPending->mState  = InstanceStateEnum::eActive;

    if (auto err = SyncPartition(*mPending->mPartitionIndex, *mInstalled.mPartitionIndex); !err.IsNone()) {
        status.mError = AOS_ERROR_WRAP(err);
        status.mState = InstanceStateEnum::eFailed;
    }

    ToInstanceStatus(mInstalled, status);

    return CompletePendingUpdate();
}

Error BootRuntime::HandleUpdateFailed(InstanceStatus& status)
{
    ToInstanceStatus(*mPending, status);

    return CompletePendingUpdate();
}

Error BootRuntime::CompletePendingUpdate()
{
    Error err = ErrorEnum::eNone;

    if (mPending->mState == InstanceStateEnum::eActive) {
        mInstalled = *mPending;

        if (auto storeErr = StoreData(cInstalledInstance, mInstalled); !storeErr.IsNone()) {
            err = AOS_ERROR_WRAP(storeErr);
        }
    }

    if (!std::filesystem::remove(GetPath(cPendingInstance))) {
        err = AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "can't remove pending instance info"));
    }

    if (auto syncErr = SyncPartition(*mInstalled.mPartitionIndex, *mPending->mPartitionIndex);
        err.IsNone() && !syncErr.IsNone()) {
        err = AOS_ERROR_WRAP(syncErr);
    }

    mPending.Reset();

    return err;
}

RetWithError<std::string> BootRuntime::GetPartitionVersion(size_t partitionIndex) const
{
    const auto mountDst  = std::filesystem::path(mBootConfig.mWorkingDir) / cMountDirName;
    const auto partition = mPartitionDevices.at(partitionIndex);

    LOG_DBG() << "Mount partition" << Log::Field("partition", partition.c_str())
              << Log::Field("mountDst", mountDst.c_str());

    std::string version;

    if (auto err = fs::MakeDirAll(mountDst.c_str()); !err.IsNone()) {
        return {version, AOS_ERROR_WRAP(err)};
    }

    auto cleanup = DeferRelease(&mountDst, [](const auto* path) { fs::RemoveAll(path->c_str()); });

    PartInfo partInfo;

    if (auto err = mPartitionManager->GetPartInfo(partition, partInfo); !err.IsNone()) {
        return {version, AOS_ERROR_WRAP(err)};
    }

    if (auto err = mPartitionManager->Mount(partInfo, mountDst.c_str(), MS_RDONLY); !err.IsNone()) {
        return {version, AOS_ERROR_WRAP(err)};
    }

    auto umount = DeferRelease(&mountDst, [this](const auto* path) {
        if (auto err = mPartitionManager->Unmount(*path); !err.IsNone()) {
            LOG_ERR() << "Failed to unmount partition" << Log::Field(err);
        }
    });

    const auto versionFilePath = mountDst / mBootConfig.mVersionFile;

    LOG_DBG() << "Read version file" << Log::Field("path", versionFilePath.c_str());

    std::ifstream versionFile(versionFilePath);
    if (!versionFile.is_open()) {
        return {version, AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "can't open version file"))};
    }

    std::string line;

    std::getline(versionFile, line);

    LOG_DBG() << "Version file content" << Log::Field("line", line.c_str());

    std::regex  versionRegex(R"(VERSION\s*=\s*\"(.+)\")");
    std::smatch match;

    if (std::regex_search(line, match, versionRegex) && match.size() == 2) {
        version = match[1];
    } else {
        return {version, AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "invalid version file format"))};
    }

    return version;
}

void BootRuntime::ToInstanceStatus(const BootData& data, InstanceStatus& status) const
{
    static_cast<InstanceIdent&>(status) = static_cast<const InstanceIdent&>(data);
    status.mManifestDigest              = data.mManifestDigest;
    status.mState                       = data.mState;
    status.mVersion                     = data.mVersion;
    status.mRuntimeID                   = mRuntimeInfo.mRuntimeID;
    status.mType                        = UpdateItemTypeEnum::eComponent;

    if (status.mSubjectID.IsEmpty()) {
        status.mPreinstalled = true;
    }
}

Error BootRuntime::InstallPendingUpdate()
{
    LOG_DBG() << "Install pending update" << Log::Field("digest", mPending->mManifestDigest)
              << Log::Field("partitionIndex", mPending->mPartitionIndex);

    auto manifest = std::make_unique<oci::ImageManifest>();

    if (auto err = GetImageManifest(mPending->mManifestDigest, *manifest); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = InstallImageOnPartition(*manifest, mPending->mPartitionIndex.GetValue()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = mBootController->SetMainBoot(mPending->mPartitionIndex.GetValue()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = mStatusReceiver->RebootRequired(mRuntimeInfo.mRuntimeID); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error BootRuntime::GetImageManifest(const String& digest, oci::ImageManifest& manifest) const
{
    LOG_DBG() << "Get image manifest" << Log::Field("digest", digest);

    StaticString<cFilePathLen> blobPath;

    if (auto err = mItemInfoProvider->GetBlobPath(digest, blobPath); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = mOCISpec->LoadImageManifest(blobPath, manifest); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error BootRuntime::InstallImageOnPartition(const oci::ImageManifest& manifest, size_t partitionIndex)
{
    LOG_DBG() << "Install image on partition" << Log::Field("partitionIndex", partitionIndex);

    if (manifest.mLayers.Size() == 0) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eInvalidArgument, "image manifest has no layers"));
    }

    const auto imagesDir         = GetPath(cImagesDir);
    const auto packedImagePath   = imagesDir / "boot.img.gz";
    const auto unpackedImagePath = imagesDir / "boot.img";

    if (auto err = fs::ClearDir(imagesDir.c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    StaticString<cFilePathLen> imageArchivePath;

    if (auto err = mItemInfoProvider->GetBlobPath(manifest.mLayers[0].mDigest, imageArchivePath); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    std::error_code ec;
    std::filesystem::copy_file(imageArchivePath.CStr(), packedImagePath, ec);

    if (ec) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, ec.message().c_str()));
    }

    if (auto res = common::utils::ExecCommand({"gunzip", packedImagePath.c_str()}); !res.mError.IsNone()) {
        return AOS_ERROR_WRAP(res.mError);
    }

    auto cleanup = DeferRelease(&imagesDir, [](const auto* path) { fs::RemoveAll(path->c_str()); });

    try {
        const auto& toDevice = mPartitionDevices.at(partitionIndex);

        LOG_DBG() << "Install image" << Log::Field("image", unpackedImagePath.c_str())
                  << Log::Field("toDevice", toDevice.c_str());

        if (auto err = mPartitionManager->InstallImage(unpackedImagePath.c_str(), toDevice); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error BootRuntime::SyncPartition(size_t from, size_t to)
{
    if (from == to) {
        return ErrorEnum::eNone;
    }

    try {
        const auto& fromDevice = mPartitionDevices.at(from);
        const auto& toDevice   = mPartitionDevices.at(to);

        LOG_DBG() << "Sync partition" << Log::Field("from", fromDevice.c_str()) << Log::Field("to", toDevice.c_str());

        return mPartitionManager->CopyDevice(fromDevice, toDevice);
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error BootRuntime::StoreData(const std::string_view filename, const BootData& data)
{
    const auto path = GetPath(filename);

    LOG_DBG() << "Store data" << Log::Field("ident", static_cast<const InstanceIdent&>(data))
              << Log::Field("digest", data.mManifestDigest) << Log::Field("state", data.mState)
              << Log::Field("path", path.c_str());

    std::ofstream file(path);
    if (!file.is_open()) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "can't open file"));
    }

    auto json = Poco::makeShared<Poco::JSON::Object>(Poco::JSON_PRESERVE_KEY_ORDER);

    try {
        json->set("itemId", data.mItemID.CStr());
        json->set("subjectId", data.mSubjectID.CStr());
        json->set("instance", data.mInstance);
        json->set("manifestDigest", data.mManifestDigest.CStr());
        json->set("state", data.mState.ToString().CStr());
        json->set("version", data.mVersion.CStr());

        if (data.mPartitionIndex.HasValue()) {
            json->set("partitionIndex", data.mPartitionIndex.GetValue());
        }

        json->stringify(file);
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error BootRuntime::LoadData(const std::string_view filename, BootData& data)
{
    const auto path = GetPath(filename);

    LOG_DBG() << "Load data" << Log::Field("path", path.c_str());

    std::ifstream file(path);
    if (!file.is_open()) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, "can't open file"));
    }

    try {
        auto parseResult = common::utils::ParseJson(file);
        AOS_ERROR_CHECK_AND_THROW(parseResult.mError, "can't parse json");

        const auto object = common::utils::CaseInsensitiveObjectWrapper(parseResult.mValue);

        auto err = data.mItemID.Assign(object.GetValue<std::string>("itemId").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse itemID");

        err = data.mSubjectID.Assign(object.GetValue<std::string>("subjectId").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse subjectID");

        data.mInstance = object.GetValue<uint64_t>("instance");

        err = data.mManifestDigest.Assign(object.GetValue<std::string>("manifestDigest").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse manifestDigest");

        err = data.mState.FromString(object.GetValue<std::string>("state").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse instance state");

        err = data.mVersion.Assign(object.GetValue<std::string>("version").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "can't parse version");

        if (object.Has("partitionIndex")) {
            data.mPartitionIndex.SetValue(object.GetValue<size_t>("partitionIndex"));
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

std::filesystem::path BootRuntime::GetPath(const std::string_view relativePath) const
{
    return std::filesystem::absolute(mBootConfig.mWorkingDir) / relativePath;
}

size_t BootRuntime::GetNextPartitionIndex(size_t currentPartition) const
{
    return (currentPartition + 1) % cNumBootPartitions;
}

} // namespace aos::sm::launcher
