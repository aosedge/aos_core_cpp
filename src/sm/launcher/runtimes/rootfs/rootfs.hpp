/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_ROOTFS_ROOTFS_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_ROOTFS_ROOTFS_HPP_

#include <filesystem>
#include <mutex>
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
    static constexpr auto cImageExtension             = ".squashfs";
    static constexpr auto cFullMediaTypePrefix        = "vnd.aos.image.component.full";
    static constexpr auto cIncrementalMediaTypePrefix = "vnd.aos.image.component.inc";
    static constexpr auto cInstalledInstanceFileName  = "installed_instance.json";
    static constexpr auto cPendingInstanceFileName    = "pending_instance.json";
    static constexpr auto cMaxNumInstances            = 1;

    /**
     * Action type type.
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

    void                                    RunHealthCheck(std::unique_ptr<InstanceStatus> status);
    RetWithError<StaticString<cVersionLen>> GetCurrentVersion() const;
    Error                                   InitInstalledData();
    Error                                   InitPendingData();
    Error                                   CreateRuntimeInfo();
    Error                                   ProcessUpdateAction(Array<InstanceStatus>& statuses);
    Error                                   ProcessUpdated(Array<InstanceStatus>& statuses);
    Error                                   ProcessFailed(Array<InstanceStatus>& statuses);
    Error                                   ProcessNoAction(Array<InstanceStatus>& statuses);
    void  FillInstanceStatus(const InstanceInfo& instanceInfo, InstanceStateEnum state, InstanceStatus& status) const;
    Error SaveInstanceInfo(const InstanceInfo& instance, const std::filesystem::path& path) const;
    Error LoadInstanceInfo(const std::filesystem::path& path, InstanceInfo& instance);
    Error GetImageManifest(const String& digest, oci::ImageManifest& manifest) const;
    Error UnpackImage(const oci::ImageManifest& manifest) const;
    Error PrepareUpdateFileContent(const oci::ImageManifest& manifest, std::string& updateType) const;
    void  ClearUpdateArtifacts() const;
    Error StoreAction(const ActionType& action, const std::string& data = "") const;
    ActionType            ReadAction() const;
    Error                 PrepareUpdate(const InstanceInfo& instance);
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
    std::optional<std::thread> mHealthCheckThread;
    InstanceInfo               mCurrentInstance {};
    StaticString<cVersionLen>  mCurrentVersion;
    RuntimeInfo                mRuntimeInfo;
    InstanceInfo               mPendingInstance;
    StaticString<cVersionLen>  mPendingVersion;
};

} // namespace aos::sm::launcher

#endif
