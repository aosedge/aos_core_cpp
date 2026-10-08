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
     * @return Error.
     */
    Error Init(const RuntimeConfig& config, iamclient::CurrentNodeInfoProviderItf& currentNodeInfoProvider,
        imagemanager::ItemInfoProviderItf& itemInfoProvider, oci::OCISpecItf& ociSpec,
        InstanceStatusReceiverItf& statusReceiver, sm::utils::SystemdConnItf& systemdConn);

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
    static constexpr auto cInstalledInstanceFileName = "installed_instance.json";
    static constexpr auto cPendingInstanceFileName   = "pending_instance.json";
    static constexpr auto cMaxNumInstances           = 1;

    /**
     * Action type type.
     *
     * Action files are shared with the initramfs aosupdate module:
     *  - do_update - written by SM to request update, contains update type (full or incremental);
     *  - updated   - written by initramfs when update image is mounted (trial boot);
     *  - do_apply  - written by SM when trial boot is confirmed by health check;
     *  - failed    - written by SM or initramfs when update failed, contains error message.
     */
    class ActionTypeType {
    public:
        enum class Enum {
            eUpdated,
            eDoApply,
            eDoUpdate,
            eFailed,
            eNumActions,
        };

        static const Array<const char* const> GetStrings()
        {
            static const char* const sStrings[] = {
                "updated",
                "do_apply",
                "do_update",
                "failed",
                "",
            };

            return Array<const char* const>(sStrings, ArraySize(sStrings));
        };
    };

    using ActionTypeEnum = ActionTypeType::Enum;
    using ActionType     = EnumStringer<ActionTypeType>;

    /**
     * Existing action files.
     */
    struct Actions {
        bool mUpdated {};
        bool mDoApply {};
        bool mDoUpdate {};
        bool mFailed {};
    };

    void  RunHealthCheck(std::unique_ptr<InstanceStatus> status);
    void  JoinHealthCheck();
    Error InitInstalledData();
    Error InitPendingData();
    Error CreateRuntimeInfo();
    Error ProcessUpdateAction(Array<InstanceStatus>& statuses);
    Error ProcessNoPending(Array<InstanceStatus>& statuses, const Actions& actions);
    Error ProcessUpdated(Array<InstanceStatus>& statuses);
    Error ProcessWaitReboot(Array<InstanceStatus>& statuses, InstanceStateEnum state, const Actions& actions);
    Error ProcessFailed(Array<InstanceStatus>& statuses, const Error& err);
    Error ProcessNoAction(Array<InstanceStatus>& statuses);
    Error StoreVerdict(bool confirmed, const Error& err = ErrorEnum::eNone);
    void  ResetPending();
    RetWithError<bool> GetCurrentOrPendingStatus(const InstanceInfo& instance, InstanceStatus& status) const;
    Error              SendRebootRequest();
    RetWithError<bool> IsPendingBoot() const;
    void  FillInstanceStatus(const InstanceInfo& instanceInfo, InstanceStateEnum state, InstanceStatus& status) const;
    Error GetImageManifest(const String& digest, oci::ImageManifest& manifest) const;
    Error CopyImage(const oci::ImageManifest& manifest) const;
    Error ClearUpdateArtifacts() const;
    Error StoreAction(const ActionType& action, std::string_view data = "") const;
    Error ReadActions(Actions& actions) const;
    Error ReadFailedReason() const;
    Error PrepareUpdate(const InstanceInfo& instance);
    std::filesystem::path GetPath(const std::string& fileName) const;

    RuntimeConfig                          mRuntimeConfig;
    RootfsConfig                           mRootfsConfig;
    iamclient::CurrentNodeInfoProviderItf* mCurrentNodeInfoProvider {};
    imagemanager::ItemInfoProviderItf*     mItemInfoProvider {};
    oci::OCISpecItf*                       mOCISpec {};
    InstanceStatusReceiverItf*             mStatusReceiver {};
    utils::SystemdUpdateChecker            mUpdateChecker;
    utils::SystemdRebooter                 mRebooter;
    InstanceIdent                          mDefaultInstanceIdent;

    mutable std::mutex         mMutex;
    std::mutex                 mHealthCheckMutex;
    std::optional<std::thread> mHealthCheckThread;
    InstanceInfo               mCurrentInstance;
    RuntimeInfo                mRuntimeInfo;
    InstanceInfo               mPendingInstance;
    bool                       mHasPending {};
    std::string                mPendingBootID;
    bool                       mPendingConfirmed {};
    bool                       mRebootRequired {};
    bool                       mRunHealthCheck {};
    InstanceStateEnum          mPendingState {InstanceStateEnum::eActivating};
    Error                      mPendingError;
};

} // namespace aos::sm::launcher

#endif
