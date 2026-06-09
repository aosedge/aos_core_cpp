/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_FILECOPY_FILECOPY_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_FILECOPY_FILECOPY_HPP_

#include <filesystem>
#include <mutex>
#include <optional>

#include <core/common/iamclient/itf/currentnodeinfoprovider.hpp>
#include <core/common/ocispec/itf/ocispec.hpp>
#include <core/sm/imagemanager/itf/iteminfoprovider.hpp>
#include <core/sm/launcher/itf/instancestatusreceiver.hpp>
#include <core/sm/launcher/itf/runtime.hpp>

#include <sm/launcher/runtimes/config.hpp>
#include <sm/launcher/runtimes/utils/systemdrebooter.hpp>
#include <sm/utils/itf/systemdconn.hpp>

#include "config.hpp"

namespace aos::sm::launcher {

/**
 * File copy runtime name.
 */
constexpr auto cRuntimeFileCopy = "filecopy";

/**
 * File copy runtime implementation.
 *
 * Accepts an artifact (squashfs image or tar.gz archive) and places it into the component directory.
 * Requests a reboot after installation so that initrd scripts can mount the image.
 */
class FileCopyRuntime : public RuntimeItf {
public:
    /**
     * Initializes file copy runtime.
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
    static constexpr auto cInstalledInstanceFileName = "installed_instance.json";
    static constexpr auto cImageFileName             = "image.squashfs";
    static constexpr auto cDefaultVersion            = "0.0.0";
    static constexpr auto cMaxNumInstances           = 1;

    Error InitInstalledData();
    Error CreateRuntimeInfo();
    void  FillInstanceStatus(const InstanceInfo& instanceInfo, InstanceStateEnum state, InstanceStatus& status) const;
    Error SaveInstanceInfo(const InstanceInfo& instance) const;
    Error LoadInstanceInfo(InstanceInfo& instance);
    Error GetImageManifest(const String& digest, oci::ImageManifest& manifest) const;
    Error CopyImage(const oci::ImageManifest& manifest) const;

    RuntimeConfig                          mRuntimeConfig;
    FileCopyConfig                         mComponentConfig;
    iamclient::CurrentNodeInfoProviderItf* mCurrentNodeInfoProvider {};
    imagemanager::ItemInfoProviderItf*     mItemInfoProvider {};
    oci::OCISpecItf*                       mOCISpec {};
    InstanceStatusReceiverItf*             mStatusReceiver {};
    utils::SystemdRebooter                 mRebooter;
    InstanceIdent                          mDefaultInstanceIdent;

    mutable std::mutex          mMutex;
    std::optional<InstanceInfo> mCurrentInstance;
    RuntimeInfo                 mRuntimeInfo;
};

} // namespace aos::sm::launcher

#endif
