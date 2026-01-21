/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_BOOT_BOOT_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_BOOT_BOOT_HPP_

#include <filesystem>
#include <memory>
#include <mutex>

#include <core/common/iamclient/itf/currentnodeinfoprovider.hpp>
#include <core/common/ocispec/itf/ocispec.hpp>
#include <core/sm/imagemanager/itf/iteminfoprovider.hpp>
#include <core/sm/launcher/itf/instancestatusreceiver.hpp>
#include <core/sm/launcher/itf/runtime.hpp>

#include <sm/launcher/runtimes/config.hpp>
#include <sm/launcher/runtimes/utils/systemdrebooter.hpp>
#include <sm/launcher/runtimes/utils/systemdupdatechecker.hpp>

#include "config.hpp"
#include "itf/bootcontroller.hpp"
#include "itf/partitionmanager.hpp"

namespace aos::sm::launcher {

/**
 * Boot runtime name.
 */
constexpr auto cRuntimeBoot = "boot";

/**
 * Boot runtime implementation.
 */
class BootRuntime : public RuntimeItf {
public:
    /**
     * Initializes boot runtime.
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
    static constexpr auto cNumBootPartitions = 2;
    static constexpr auto cInstalledInstance = "installed.json";
    static constexpr auto cPendingInstance   = "pending.json";
    static constexpr auto cImagesDir         = "images";
    static constexpr auto cMountDirName      = "mnt";
    static constexpr auto cUpdateStateFile   = "update.state";

    struct BootData : public InstanceIdent {
        StaticString<cVersionLen>     mVersion;
        InstanceState                 mState {InstanceStateEnum::eActive};
        StaticString<oci::cDigestLen> mManifestDigest;
        Error                         mError;
        Optional<size_t>              mPartitionIndex;
    };

    virtual std::shared_ptr<PartitionManagerItf> CreatePartitionManager() const;
    virtual std::shared_ptr<BootControllerItf>   CreateBootController() const;

    Error                     InitBootPartitions();
    Error                     InitBootData();
    Error                     InitInstalledData();
    Error                     InitPendingData();
    Error                     CreateRuntimeInfo();
    Error                     HandleUpdate(Array<InstanceStatus>& statuses);
    Error                     HandleUpdateSucceeded(InstanceStatus& status);
    Error                     HandleUpdateFailed(InstanceStatus& status);
    Error                     CompletePendingUpdate();
    RetWithError<std::string> GetPartitionVersion(size_t partitionIndex) const;
    Error                     GetPartInfo(const std::string& partDevice, PartInfo& partInfo) const;
    Error                     StoreUpdateState() const;
    void                      ToInstanceStatus(const BootData& data, InstanceStatus& status) const;
    Error                     InstallPendingUpdate();
    Error                     GetImageManifest(const String& digest, oci::ImageManifest& manifest) const;
    Error                     InstallImageOnPartition(const oci::ImageManifest& manifest, size_t partitionIndex);
    Error                     SyncPartition(size_t from, size_t to);
    Error                     StoreData(const std::string_view filename, const BootData& data);
    Error                     LoadData(const std::string_view filename, BootData& data);
    std::filesystem::path     GetPath(const std::string_view relativePath) const;
    size_t                    GetNextPartitionIndex(size_t currentPartition) const;

    mutable std::mutex                     mMutex;
    std::shared_ptr<PartitionManagerItf>   mPartitionManager;
    std::shared_ptr<BootControllerItf>     mBootController;
    iamclient::CurrentNodeInfoProviderItf* mCurrentNodeInfoProvider {};
    imagemanager::ItemInfoProviderItf*     mItemInfoProvider {};
    oci::OCISpecItf*                       mOCISpec {};
    InstanceStatusReceiverItf*             mStatusReceiver {};
    RuntimeConfig                          mConfig;
    BootConfig                             mBootConfig;
    InstanceIdent                          mDefaultInstanceIdent;
    utils::SystemdRebooter                 mSystemdRebooter;
    utils::SystemdUpdateChecker            mSystemdUpdateChecker;
    RuntimeInfo                            mRuntimeInfo;
    size_t                                 mMainPartition {};
    size_t                                 mCurrentPartition {};
    std::string                            mCurrentPartitionVersion;
    BootData                               mInstalled;
    Optional<BootData>                     mPending;
    std::vector<std::string>               mPartitionDevices;
};

} // namespace aos::sm::launcher

#endif
