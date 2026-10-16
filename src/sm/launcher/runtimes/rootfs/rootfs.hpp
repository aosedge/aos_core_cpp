/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_ROOTFS_ROOTFS_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_ROOTFS_ROOTFS_HPP_

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include <core/common/iamclient/itf/currentnodeinfoprovider.hpp>
#include <core/common/ocispec/itf/ocispec.hpp>
#include <core/sm/imagemanager/itf/iteminfoprovider.hpp>
#include <core/sm/launcher/itf/instancestatusreceiver.hpp>
#include <core/sm/launcher/itf/runtime.hpp>

#include <sm/launcher/runtimes/config.hpp>
#include <sm/launcher/runtimes/itf/storage.hpp>
#include <sm/launcher/runtimes/utils/systemdrebooter.hpp>
#include <sm/launcher/runtimes/utils/systemdupdatechecker.hpp>
#include <sm/utils/itf/systemdconn.hpp>

#include "config.hpp"

namespace aos::sm::launcher {

/**
 * Rootfs runtime name.
 */
constexpr auto cRuntimeRootfs = "rootfs";

/**
 * Rootfs runtime implementation.
 */
class RootfsRuntime : public RuntimeItf {
public:
    /**
     * Destructor.
     */
    ~RootfsRuntime();

    /**
     * Initializes rootfs runtime.
     *
     * @param config runtime config.
     * @param currentNodeInfoProvider current node info provider.
     * @param itemInfoProvider item info provider.
     * @param ociSpec OCI spec interface.
     * @param statusReceiver instance status receiver.
     * @param systemdConn systemd connection.
     * @param storage runtime storage.
     * @return Error.
     */
    Error Init(const RuntimeConfig& config, iamclient::CurrentNodeInfoProviderItf& currentNodeInfoProvider,
        imagemanager::ItemInfoProviderItf& itemInfoProvider, oci::OCISpecItf& ociSpec,
        InstanceStatusReceiverItf& statusReceiver, sm::utils::SystemdConnItf& systemdConn,
        runtimes::StorageItf& storage);

    /**
     * Starts runtime.
     *
     * @return Error.
     */
    Error Start() override;

    /**
     * Stops runtime.
     *
     * @return Error.
     */
    Error Stop() override;

    /**
     * Returns runtime info.
     *
     * @param[out] runtimeInfo runtime info.
     * @return Error.
     */
    Error GetRuntimeInfo(RuntimeInfo& runtimeInfo) const override;

    /**
     * Initializes instances.
     *
     * Launcher provides list of known instances to runtime at startup. Runtime should stop all instances that are not
     * in the list and properly initialize already running instances. Runtime should not start any instance at this
     * stage, it should only prepare them for future start.
     *
     * @param instancesInfo instances info.
     * @return Error.
     */
    Error InitInstances(const Array<InstanceInfo>& instancesInfo) override;

    /**
     * Start instance.
     *
     * @param instance instance to start.
     * @param[out] status instance status.
     * @return Error.
     */
    Error StartInstance(const InstanceInfo& instance, InstanceStatus& status) override;

    /**
     * Stop instance.
     *
     * @param instance instance to stop.
     * @param[out] status instance status.
     * @return Error.
     */
    Error StopInstance(const InstanceIdent& instance, InstanceStatus& status) override;

    /**
     * Reboots runtime.
     *
     * @return Error.
     */
    Error Reboot() override;

    /**
     * Returns instance monitoring data.
     *
     * @param instanceIdent instance ident.
     * @param[out] monitoringData instance monitoring data.
     * @return Error.
     */
    Error GetInstanceMonitoringData(
        const InstanceIdent& instanceIdent, monitoring::InstanceMonitoringData& monitoringData) override;

private:
    static constexpr auto cUpdateStateFileName = "update_state";
    static constexpr auto cMaxNumInstances     = 1;
    // Current and pending instances.
    static constexpr auto cMaxNumStoredInstances = 2;

    /**
     * Update state type.
     *
     * Update state file is shared with the initramfs aosupdate module. Each state change atomically replaces the file:
     *  - prepared  - SM: update image is ready, initramfs should mount it as rootfs (trial boot);
     *  - trial     - initramfs: update image is mounted as rootfs;
     *  - confirmed - SM: trial boot is confirmed by health check, initramfs should apply the update;
     *  - rejected  - SM: trial boot is rejected, initramfs should revert the update;
     *  - applied   - initramfs: update is applied to rootfs;
     *  - reverted  - initramfs: update is reverted;
     *  - failed    - initramfs: update image can't be mounted.
     */
    class UpdateStateType {
    public:
        enum class Enum {
            ePrepared,
            eTrial,
            eConfirmed,
            eRejected,
            eApplied,
            eReverted,
            eFailed,
            eNumStates,
        };

        static const Array<const char* const> GetStrings()
        {
            static const char* const sStrings[] = {
                "prepared",
                "trial",
                "confirmed",
                "rejected",
                "applied",
                "reverted",
                "failed",
                "",
            };

            return Array<const char* const>(sStrings, ArraySize(sStrings));
        };
    };

    using UpdateStateEnum = UpdateStateType::Enum;
    using UpdateState     = EnumStringer<UpdateStateType>;

    void               RunHealthCheck(std::unique_ptr<InstanceStatus> status);
    void               JoinHealthCheck();
    Error              CreateRuntimeInfo();
    Error              LoadInstances();
    Error              LoadUpdateState();
    Error              ProcessUpdateState(Array<InstanceStatus>& statuses);
    Error              ProcessNoUpdate(Array<InstanceStatus>& statuses);
    Error              ProcessUntracked(Array<InstanceStatus>& statuses);
    Error              ProcessTrial(Array<InstanceStatus>& statuses);
    Error              ProcessApplyFailed(Array<InstanceStatus>& statuses);
    Error              ProcessApplied(Array<InstanceStatus>& statuses);
    Error              ProcessFailed(Array<InstanceStatus>& statuses, const Error& err);
    Error              WaitReboot(Array<InstanceStatus>& statuses);
    Error              RejectTrialBoot(Array<InstanceStatus>& statuses, Error err);
    Error              PromotePending();
    Error              RestoreDefaultCurrent();
    void               SetPendingFailed(const Error& err);
    Error              AddStatus(Array<InstanceStatus>& statuses, const runtimes::RuntimeInstanceData& instance) const;
    Error              StoreUpdateState(UpdateStateEnum state, const std::string& reason = "");
    Error              GetUpdateError(const char* defaultReason) const;
    void               ResetPending();
    RetWithError<bool> GetCurrentOrPendingStatus(const InstanceInfo& instance, InstanceStatus& status) const;
    Error              SendRebootRequest();
    void               FillInstanceStatus(const runtimes::RuntimeInstanceData& instance, InstanceStatus& status) const;
    Error              GetImageManifest(const String& digest, oci::ImageManifest& manifest) const;
    Error              CopyImage(const oci::ImageManifest& manifest) const;
    Error              ClearUpdateArtifacts() const;
    Error              PrepareUpdate(const InstanceInfo& instance);
    std::filesystem::path GetPath(const std::string& fileName) const;

    RuntimeConfig                          mRuntimeConfig;
    RootfsConfig                           mRootfsConfig;
    iamclient::CurrentNodeInfoProviderItf* mCurrentNodeInfoProvider {};
    imagemanager::ItemInfoProviderItf*     mItemInfoProvider {};
    oci::OCISpecItf*                       mOCISpec {};
    InstanceStatusReceiverItf*             mStatusReceiver {};
    runtimes::StorageItf*                  mStorage {};
    utils::SystemdUpdateChecker            mUpdateChecker;
    utils::SystemdRebooter                 mRebooter;
    InstanceIdent                          mDefaultInstanceIdent;

    mutable std::mutex            mMutex;
    std::mutex                    mHealthCheckMutex;
    std::optional<std::thread>    mHealthCheckThread;
    runtimes::RuntimeInstanceData mCurrent;
    runtimes::RuntimeInstanceData mPending;
    RuntimeInfo                   mRuntimeInfo;
    bool                          mHasUpdateState {};
    UpdateState                   mUpdateState;
    std::string                   mUpdateType;
    std::string                   mUpdateError;
    bool                          mRebootRequired {};
    bool                          mRunHealthCheck {};
};

} // namespace aos::sm::launcher

#endif
