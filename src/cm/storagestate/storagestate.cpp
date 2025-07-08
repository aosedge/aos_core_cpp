/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <memory>

#include <aos/common/tools/uuid.hpp>

#include <common/logger/logmodule.hpp>
#include <common/utils/exception.hpp>
#include <common/utils/filesystem.hpp>
#include <common/utils/fsplatform.hpp>

#include "storagestate.hpp"

namespace aos::cm::storagestate {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/
namespace {

Log& operator<<(Log& log, const SetupParams& params)
{
    return log << Log::Field("instanceIdent", params.mInstanceIdent) << Log::Field("uid", params.mUID)
               << Log::Field("gid", params.mGID) << Log::Field("stateQuota", params.mStateQuota)
               << Log::Field("storageQuota", params.mStorageQuota);
}

Error ToRelativePath(const String& base, const String& full, String& result)
{
    std::filesystem::path fullpath {full.CStr()};
    std::filesystem::path basepath {base.CStr()};

    std::error_code ec;

    auto relativePath = std::filesystem::relative(fullpath, basepath, ec);
    if (ec) {
        return Error(ErrorEnum::eFailed, ec.message().c_str());
    }

    return result.Assign(relativePath.string().c_str());
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error StorageState::Init(const config::Config& config, StorageItf& storage,
    cm::communication::CommunicationItf& communication, fs::FSPlatformItf& fsPlatform,
    crypto::CryptoProviderItf& cryptoProvider)
{
    LOG_INF() << "Initialize storage state";

    mStorage        = &storage;
    mMessageSender  = &communication;
    mFSPlatform     = &fsPlatform;
    mCryptoProvider = &cryptoProvider;

    try {
        auto err = mStorageDir.Assign(config.mStorageDir.c_str());
        AOS_ERROR_CHECK_AND_THROW(err);

        err = mStateDir.Assign(config.mStateDir.c_str());
        AOS_ERROR_CHECK_AND_THROW(err);

        err = mFSWatcher.Init();
        AOS_ERROR_CHECK_AND_THROW(err);

        err = fs::MakeDirAll(mStorageDir);
        AOS_ERROR_CHECK_AND_THROW(err);

        err = fs::MakeDirAll(mStateDir);
        AOS_ERROR_CHECK_AND_THROW(err);

        StaticString<cFilePathLen> storageMountPoint, stateMountPoint;

        Tie(storageMountPoint, err) = mFSPlatform->GetMountPoint(mStorageDir);
        AOS_ERROR_CHECK_AND_THROW(err);

        Tie(stateMountPoint, err) = mFSPlatform->GetMountPoint(mStateDir);
        AOS_ERROR_CHECK_AND_THROW(err);

        mStateAndStorageOnSamePartition = (storageMountPoint == stateMountPoint);

        err = InitStateWatching();
        AOS_ERROR_CHECK_AND_THROW(err);
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error StorageState::Start()
{
    LOG_INF() << "Start storage state";

    return AOS_ERROR_WRAP(mFSWatcher.Start());
}

Error StorageState::Stop()
{
    LOG_INF() << "Stop storage state";

    return AOS_ERROR_WRAP(mFSWatcher.Stop());
}

Error StorageState::Setup(const SetupParams& setupParams, String& storagePath, String& statePath)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Setup storage and state" << setupParams;

    auto storageStateInfo = std::make_unique<StorageStateInstanceInfo>();

    auto err = mStorage->GetStorageStateInfo(setupParams.mInstanceIdent, *storageStateInfo);
    if (err.Is(ErrorEnum::eNotFound)) {
        storageStateInfo = std::make_unique<StorageStateInstanceInfo>();

        storageStateInfo->mInstanceIdent = setupParams.mInstanceIdent;

        auto [uuid, uuidErr] = mCryptoProvider->CreateUUIDv4();
        if (!uuidErr.IsNone()) {
            return AOS_ERROR_WRAP(uuidErr);
        }

        storageStateInfo->mInstanceID = uuid::UUIDToString(uuid);

        err = mStorage->AddStorageStateInfo(*storageStateInfo);
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    } else if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    const auto& instanceID = storageStateInfo->mInstanceID;

    err = PrepareStorage(instanceID, setupParams, storagePath);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    StopStateWatching(setupParams.mInstanceIdent);

    if (!QuotasAreEqual(*storageStateInfo, setupParams)) {
        err = SetQuotas(setupParams);
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        storageStateInfo->mStorageQuota = setupParams.mStorageQuota;
        storageStateInfo->mStateQuota   = setupParams.mStateQuota;

        err = mStorage->UpdateStorageStateInfo(*storageStateInfo);
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    err = PrepareState(instanceID, setupParams, storageStateInfo->mStateChecksum, statePath);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error StorageState::Cleanup(const InstanceIdent& instanceIdent)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Clean storage and state" << Log::Field("instanceIdent", instanceIdent);

    return StopStateWatching(instanceIdent);
}

Error StorageState::Remove(const InstanceIdent& instanceIdent)
{
    LOG_DBG() << "Remove storage and state" << Log::Field("instanceIdent", instanceIdent);

    if (auto err = Cleanup(instanceIdent); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    auto stateStorageInfo = std::make_unique<StorageStateInstanceInfo>();

    if (auto err = mStorage->GetStorageStateInfo(instanceIdent, *stateStorageInfo); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = RemoveFromSystem(stateStorageInfo->mInstanceID, instanceIdent); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error StorageState::UpdateState(const cloudprotocol::UpdateState& state)
{
    std::lock_guard lock {mMutex};

    auto it = std::find_if(mStates.begin(), mStates.end(),
        [&state](const auto& item) { return item.mInstanceIdent == state.mInstanceIdent; });
    if (it == mStates.end()) {
        return AOS_ERROR_WRAP(ErrorEnum::eNotFound);
    }

    LOG_DBG() << "Update state" << Log::Field("instanceIdent", state.mInstanceIdent)
              << Log::Field("checksum", state.mChecksum) << Log::Field("size", state.mState.Size());

    if (state.mState.Size() > it->mQuota) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eInvalidArgument, "update state exceeds quota"));
    }

    try {
        auto err = ValidateChecksum(state.mState, state.mChecksum);
        AOS_ERROR_CHECK_AND_THROW(err);

        auto storageStateInfo = std::make_unique<StorageStateInstanceInfo>();

        err = mStorage->GetStorageStateInfo(state.mInstanceIdent, *storageStateInfo);
        AOS_ERROR_CHECK_AND_THROW(err);

        err = storageStateInfo->mStateChecksum.Assign(state.mChecksum);
        AOS_ERROR_CHECK_AND_THROW(err);

        err = mStorage->UpdateStorageStateInfo(*storageStateInfo);
        AOS_ERROR_CHECK_AND_THROW(err);

        err = fs::WriteStringToFile(it->mFilePath, state.mState, S_IRUSR | S_IWUSR);
        AOS_ERROR_CHECK_AND_THROW(err);

        err = it->mChecksum.Assign(state.mChecksum);
        AOS_ERROR_CHECK_AND_THROW(err);

    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error StorageState::AcceptState(const cloudprotocol::StateAcceptance& state)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "State acceptance" << Log::Field("instanceIdent", state.mInstanceIdent)
              << Log::Field("result", state.mResult) << Log::Field("reason", state.mReason)
              << Log::Field("checksum", state.mChecksum);

    auto it = std::find_if(mStates.begin(), mStates.end(),
        [&state](const auto& item) { return item.mInstanceIdent == state.mInstanceIdent; });
    if (it == mStates.end()) {
        return AOS_ERROR_WRAP(ErrorEnum::eNotFound);
    }

    if (it->mChecksum != state.mChecksum) {
        LOG_DBG() << "State checksum mismatch" << Log::Field("cached", state.mChecksum);

        return AOS_ERROR_WRAP(ErrorEnum::eInvalidChecksum);
    }

    if (state.mResult != cloudprotocol::StateResultEnum::eAccepted) {
        return SendInstanceStateRequest(state.mInstanceIdent);
    }

    try {
        auto storageStateInfo = std::make_unique<StorageStateInstanceInfo>();

        auto err = mStorage->GetStorageStateInfo(state.mInstanceIdent, *storageStateInfo);
        AOS_ERROR_CHECK_AND_THROW(err);

        err = storageStateInfo->mStateChecksum.Assign(it->mChecksum);
        AOS_ERROR_CHECK_AND_THROW(err);

        err = mStorage->UpdateStorageStateInfo(*storageStateInfo);
        AOS_ERROR_CHECK_AND_THROW(err);
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error StorageState::GetInstanceCheckSum(const InstanceIdent& instanceIdent, String& checkSum)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Get instance checksum" << Log::Field("instanceIdent", instanceIdent);

    auto it = std::find_if(mStates.begin(), mStates.end(),
        [&instanceIdent](const auto& item) { return item.mInstanceIdent == instanceIdent; });
    if (it == mStates.end()) {
        return AOS_ERROR_WRAP(ErrorEnum::eNotFound);
    }

    return checkSum.Assign(it->mChecksum);
}

StorageState::~StorageState()
{
    LOG_DBG() << "Destroy storage state object";

    std::lock_guard lock {mMutex};

    while (!mStates.empty()) {
        auto it = mStates.begin();

        StopStateWatching(it->mInstanceIdent);
    }
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

void StorageState::OnFSEvent(const std::string& path, uint32_t mask)
{
    std::lock_guard lock {mMutex};

    auto it = std::find_if(
        mStates.begin(), mStates.end(), [&path, this](const auto& state) { return state.mFilePath == path.c_str(); });
    if (it == mStates.end()) {
        LOG_WRN() << "Error processing state change" << Log::Field("path", path.c_str()) << Log::Field("mask", mask)
                  << Log::Field(ErrorEnum::eNotFound);

        return;
    }

    it->mChangeTimer->stop();
    it->mChangeTimer->setStartInterval(cStateChangeTimeout.Milliseconds());
    it->mChangeTimer->start(Poco::TimerCallback<StorageState>(*this, &StorageState::NotifyStateChanged));
}

Error StorageState::InitStateWatching()
{
    LOG_DBG() << "Initialize state watching";

    auto infos = std::make_unique<StaticArray<StorageStateInstanceInfo, cMaxNumServices * cMaxNumInstances>>();

    if (auto err = mStorage->GetAllStorageStateInfo(*infos); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    StaticString<crypto::cSHA2DigestSize> checksum;

    for (const auto& info : *infos) {
        if (info.mStateQuota == 0) {
            continue;
        }

        if (auto err = StartStateWatching(info.mInstanceIdent, GetStatePath(info.mInstanceID), info.mStateQuota);
            !err.IsNone()) {
            LOG_ERR() << "Can't setup state watching" << Log::Field("instanceID", info.mInstanceID) << Log::Field(err);

            continue;
        }
    }

    return ErrorEnum::eNone;
}

Error StorageState::PrepareState(
    const String& instanceID, const SetupParams& setupParams, const String& checksum, String& statePath)
{
    const auto fullPath = GetStatePath(instanceID);

    LOG_DBG() << "Prepare state" << Log::Field("path", fullPath);

    if (setupParams.mStateQuota == 0) {
        if (auto err = fs::RemoveAll(fullPath); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        return ErrorEnum::eNone;
    }

    if (auto err = SetupStateWatching(fullPath, setupParams); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    try {
        auto itState = std::find_if(mStates.begin(), mStates.end(),
            [&setupParams](const auto& item) { return item.mInstanceIdent == setupParams.mInstanceIdent; });
        if (itState == mStates.end()) {
            AOS_ERROR_THROW(ErrorEnum::eNotFound);
        }

        auto err = itState->mChecksum.Assign(checksum);
        AOS_ERROR_CHECK_AND_THROW(err);

        err = CheckChecksumAndSendUpdateRequest(*itState);
        AOS_ERROR_CHECK_AND_THROW(err);

        err = ToRelativePath(mStateDir, fullPath, statePath);
        AOS_ERROR_CHECK_AND_THROW(err);
    } catch (const std::exception& e) {
        StopStateWatching(setupParams.mInstanceIdent);

        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error StorageState::PrepareStorage(const String& instanceID, const SetupParams& setupParams, String& storagePath)
{
    const auto fullPath = GetStoragePath(instanceID);

    LOG_DBG() << "Prepare storage" << Log::Field("path", fullPath);

    if (setupParams.mStorageQuota == 0) {
        if (auto err = fs::RemoveAll(fullPath); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        return ErrorEnum::eNone;
    }

    if (auto err = fs::MakeDirAll(fullPath); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = common::utils::ChangeOwner(fullPath.CStr(), setupParams.mUID, setupParams.mGID); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = ToRelativePath(mStorageDir, fullPath, storagePath); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error StorageState::CheckChecksumAndSendUpdateRequest(const State& state)
{
    LOG_DBG() << "Check checksum and send update request" << state;

    auto stateContent = std::make_unique<StaticString<cloudprotocol::cStateLen>>();

    if (auto err = fs::ReadFileToString(state.mFilePath, *stateContent); !err.IsNone()) {
        return err;
    }

    StaticString<crypto::cSHA2DigestSize> calculatedChecksum;

    if (auto err = CalculateChecksum(*stateContent, calculatedChecksum); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (state.mChecksum == calculatedChecksum) {
        return ErrorEnum::eNone;
    }

    if (auto err = SendInstanceStateRequest(state.mInstanceIdent); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error StorageState::CreateStateFileIfNotExist(const String& path, const SetupParams& params)
{
    if (std::filesystem::exists(path.CStr())) {
        return ErrorEnum::eNone;
    }

    if (auto err = fs::WriteStringToFile(path, "", S_IRUSR | S_IWUSR); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = common::utils::ChangeOwner(path.CStr(), params.mUID, params.mGID); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error StorageState::SetupStateWatching(const String& path, const SetupParams& params)
{
    LOG_DBG() << "Setup state watching" << Log::Field("path", path);

    if (auto err = CreateStateFileIfNotExist(path, params); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = StartStateWatching(params.mInstanceIdent, path, params.mStateQuota); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error StorageState::StartStateWatching(const InstanceIdent& instanceIdent, const String& path, size_t quota)
{
    LOG_DBG() << "Start state watching" << Log::Field("path", path);

    if (auto err = mFSWatcher.Subscribe(path.CStr(), *this); !err.IsNone()) {
        return err;
    }

    mStates.push_back(State(instanceIdent, path, quota));

    return ErrorEnum::eNone;
}

Error StorageState::StopStateWatching(const InstanceIdent& instanceIdent)
{
    LOG_DBG() << "Stop state watching" << instanceIdent;

    auto it = std::find_if(mStates.begin(), mStates.end(),
        [&instanceIdent](const auto& item) { return item.mInstanceIdent == instanceIdent; });
    if (it == mStates.end()) {
        return ErrorEnum::eNotFound;
    }

    auto err = mFSWatcher.Unsubscribe(it->mFilePath.CStr(), *this);

    it->mChangeTimer->stop();

    mStates.erase(it);

    return err;
}

Error StorageState::SetQuotas(const SetupParams& setupParams)
{
    LOG_DBG() << "Set quotas" << setupParams;

    if (mStateAndStorageOnSamePartition) {
        if (auto err = mFSPlatform->SetUserQuota(
                mStorageDir, setupParams.mStorageQuota + setupParams.mStateQuota, setupParams.mUID);
            !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        return ErrorEnum::eNone;
    }

    if (auto err = mFSPlatform->SetUserQuota(mStateDir, setupParams.mStateQuota, setupParams.mUID); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = mFSPlatform->SetUserQuota(mStorageDir, setupParams.mStorageQuota, setupParams.mUID); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

void StorageState::NotifyStateChanged(Poco::Timer& timer)
{
    LOG_DBG() << "Notify state changed";

    std::lock_guard lock {mMutex};

    auto itState = std::find_if(
        mStates.begin(), mStates.end(), [&timer](const auto& item) { return item.mChangeTimer.get() == &timer; });
    if (itState == mStates.end()) {
        LOG_ERR() << "Failed to notify state changed" << Log::Field(ErrorEnum::eNotFound);

        return;
    }

    try {
        LOG_DBG() << "State changed timer function" << *itState;

        auto newState = std::make_unique<cloudprotocol::NewState>(itState->mInstanceIdent);

        auto err = fs::ReadFileToString(itState->mFilePath, newState->mState);
        AOS_ERROR_CHECK_AND_THROW(err);

        StaticString<crypto::cSHA2DigestSize> checksum;

        err = CalculateChecksum(newState->mState, checksum);
        AOS_ERROR_CHECK_AND_THROW(err);

        if (itState->mChecksum == checksum) {
            LOG_DBG() << "State checksum is the same, no need to notify" << *itState;

            return;
        }

        err = itState->mChecksum.Assign(checksum);
        AOS_ERROR_CHECK_AND_THROW(err);

        err = newState->mChecksum.Assign(checksum);
        AOS_ERROR_CHECK_AND_THROW(err);

        auto message = std::make_unique<cloudprotocol::MessageVariant>(*newState);

        err = mMessageSender->SendMessage(*message);
        AOS_ERROR_CHECK_AND_THROW(err);
    } catch (const std::exception& e) {
        LOG_ERR() << "Failed to notify state changed" << *itState << Log::Field(common::utils::ToAosError(e));
    }
}

Error StorageState::RemoveFromSystem(const String& instanceID, const InstanceIdent& instanceIdent)
{
    const auto statePath   = GetStatePath(instanceID);
    const auto storagePath = GetStoragePath(instanceID);

    LOG_DBG() << "Remove storage and state from system" << Log::Field("instanceID", instanceID)
              << Log::Field("instanceIdent", instanceIdent) << Log::Field("statePath", statePath)
              << Log::Field("storagePath", storagePath);

    if (auto err = fs::RemoveAll(statePath); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = fs::RemoveAll(storagePath); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = mStorage->RemoveStorageStateInfo(instanceIdent); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

bool StorageState::QuotasAreEqual(const StorageStateInstanceInfo& lhs, const SetupParams& rhs) const
{
    return lhs.mStorageQuota == rhs.mStorageQuota && lhs.mStateQuota == rhs.mStateQuota;
}

Error StorageState::ValidateChecksum(const String& text, const String& checksum)
{
    StaticString<crypto::cSHA2DigestSize> calculatedChecksum;

    if (auto err = CalculateChecksum(text, calculatedChecksum); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (calculatedChecksum != checksum) {
        return ErrorEnum::eInvalidChecksum;
    }

    return ErrorEnum::eNone;
}

Error StorageState::SendInstanceStateRequest(const InstanceIdent& instanceIdent)
{
    LOG_DBG() << "Send instance state request" << Log::Field("instanceIdent", instanceIdent);

    auto stateRequest      = std::make_unique<cloudprotocol::StateRequest>(instanceIdent);
    stateRequest->mDefault = false;

    auto message = std::make_unique<cloudprotocol::MessageVariant>(*stateRequest);

    if (auto err = mMessageSender->SendMessage(*message); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

StaticString<cFilePathLen> StorageState::GetStatePath(const String& instanceID) const
{
    auto path = fs::JoinPath(mStateDir, instanceID);
    path.Append(cStateSuffix);

    return path;
}

StaticString<cFilePathLen> StorageState::GetStoragePath(const String& instanceID) const
{
    return fs::JoinPath(mStorageDir, instanceID);
}

Error StorageState::CalculateChecksum(const String& data, String& checksum)
{
    auto [hasher, err] = mCryptoProvider->CreateHash(cHashAlgorithm);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    err = hasher->Update({reinterpret_cast<const uint8_t*>(data.CStr()), data.Size()});
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    StaticArray<uint8_t, crypto::cSHA2DigestSize> checksumBytes;

    err = hasher->Finalize(checksumBytes);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    err = checksum.ByteArrayToHex(checksumBytes);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::cm::storagestate
