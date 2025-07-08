/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_STORAGESTATE_STORAGESTATE_HPP_
#define AOS_CM_STORAGESTATE_STORAGESTATE_HPP_

#include <memory>
#include <set>

#include <Poco/Timer.h>

#include <aos/cm/communication/communication.hpp>
#include <aos/cm/storagestate/storagestate.hpp>
#include <aos/common/crypto/cryptoprovider.hpp>
#include <aos/common/tools/fs.hpp>
#include <aos/common/tools/timer.hpp>

#include <common/logger/logger.hpp>
#include <common/utils/fswatcher.hpp>

#include <cm/config/config.hpp>

namespace aos::cm::storagestate {

/**
 * Storage state.
 */
class StorageState : public storagestate::StorageStateItf, private common::utils::FSEventSubscriber {
public:
    /**
     * Initializes storage state instance.
     *
     * @param config config object.
     * @param storage storage instance.
     * @param communication communication instance.
     * @param fsPlatform file system platform instance.
     * @param cryptoProvider crypto provider instance.
     * @return Error.
     */
    Error Init(const config::Config& config, StorageItf& storage, cm::communication::CommunicationItf& communication,
        fs::FSPlatformItf& fsPlatform, crypto::CryptoProviderItf& cryptoProvider);

    /**
     * Starts storage state instance.
     *
     * @return Error.
     */
    Error Start();

    /**
     * Stops storage state instance.
     *
     * @return Error.
     */
    Error Stop();

    /**
     * Setups storage state instance.
     *
     * @param setupParams setup parameters.
     * @param storagePath[out] storage path.
     * @param statePath[out] state path.
     * @return Error.
     */
    Error Setup(const SetupParams& setupParams, String& storagePath, String& statePath) override;

    /**
     * Clean-ups storage state  instance.
     *
     * @param instanceIdent instance ident.
     * @return Error.
     */
    Error Cleanup(const InstanceIdent& instanceIdent) override;

    /**
     * Removes storage state instance.
     *
     * @param instanceIdent instance ident.
     * @return Error.
     */
    Error Remove(const InstanceIdent& instanceIdent) override;

    /**
     * Updates storage state with new state.
     *
     * @param state new state.
     * @return Error.
     */
    Error UpdateState(const cloudprotocol::UpdateState& state) override;

    /**
     * Accepts state from storage.
     *
     * @param state state to accept.
     * @return Error.
     */
    Error AcceptState(const cloudprotocol::StateAcceptance& state) override;

    /**
     * Returns instance's checksum.
     *
     * @param instanceIdent instance ident.
     * @param checkSum[out] checksum.
     * @return Error
     */
    Error GetInstanceCheckSum(const InstanceIdent& instanceIdent, String& checkSum) override;

    /**
     * Destructor.
     */
    ~StorageState();

private:
    static constexpr auto cStateSuffix        = "_state.dat";
    static constexpr auto cStateChangeTimeout = Time::cSeconds * 1;
    static constexpr auto cHashAlgorithm      = crypto::HashEnum::eSHA3_224;

    struct State {
        State(const InstanceIdent& instanceIdent, const String& filePath, size_t quota)
            : mInstanceIdent(instanceIdent)
            , mFilePath(filePath)
            , mQuota(quota)
        {
        }

        State(State&&)            = default;
        State& operator=(State&&) = default;

        InstanceIdent                         mInstanceIdent;
        StaticString<cFilePathLen>            mFilePath;
        size_t                                mQuota = {};
        StaticString<crypto::cSHA2DigestSize> mChecksum;
        std::unique_ptr<Poco::Timer>          mChangeTimer = std::make_unique<Poco::Timer>();

        friend Log& operator<<(Log& log, const State& state)
        {
            return log << Log::Field("instanceIdent", state.mInstanceIdent) << Log::Field("path", state.mFilePath)
                       << Log::Field("quota", state.mQuota);
        }
    };

    void  OnFSEvent(const std::string& path, uint32_t mask) override;
    Error InitStateWatching();
    Error PrepareState(
        const String& instanceID, const SetupParams& setupParams, const String& checksum, String& statePath);
    Error PrepareStorage(const String& instanceID, const SetupParams& setupParams, String& storagePath);
    Error CheckChecksumAndSendUpdateRequest(const State& state);
    Error CreateStateFileIfNotExist(const String& path, const SetupParams& params);
    Error SetupStateWatching(const String& path, const SetupParams& params);
    Error StartStateWatching(const InstanceIdent& instanceIdent, const String& path, size_t quota);
    Error StopStateWatching(const InstanceIdent& instanceIdent);
    Error SetQuotas(const SetupParams& setupParams);
    void  NotifyStateChanged(Poco::Timer& timer);
    Error RemoveFromSystem(const String& instanceID, const InstanceIdent& instanceIdent);
    bool  QuotasAreEqual(const StorageStateInstanceInfo& lhs, const SetupParams& rhs) const;
    Error ValidateChecksum(const String& text, const String& checksum);
    Error SendInstanceStateRequest(const InstanceIdent& instanceIdent);
    StaticString<cFilePathLen> GetStatePath(const String& instanceID) const;
    StaticString<cFilePathLen> GetStoragePath(const String& instanceID) const;
    Error                      CalculateChecksum(const String& data, String& checksum);

    std::mutex                           mMutex;
    StaticString<cFilePathLen>           mStorageDir;
    StaticString<cFilePathLen>           mStateDir;
    StorageItf*                          mStorage                        = {};
    cm::communication::CommunicationItf* mMessageSender                  = {};
    fs::FSPlatformItf*                   mFSPlatform                     = {};
    crypto::CryptoProviderItf*           mCryptoProvider                 = {};
    bool                                 mStateAndStorageOnSamePartition = {};
    std::vector<State>                   mStates;
    common::utils::FSWatcher             mFSWatcher;
};

} // namespace aos::cm::storagestate

#endif
