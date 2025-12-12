/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_ROOTFS_ROOTFS_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_ROOTFS_ROOTFS_HPP_

#include <mutex>
#include <string>

#include <core/common/iamclient/itf/currentnodeinfoprovider.hpp>
#include <core/common/ocispec/itf/ocispec.hpp>
#include <core/common/tools/time.hpp>
#include <core/sm/launcher/itf/instancestatusreceiver.hpp>
#include <core/sm/launcher/itf/rebooter.hpp>
#include <core/sm/launcher/itf/runtime.hpp>
#include <core/sm/launcher/itf/updatechecker.hpp>
#include <sm/utils/itf/systemdconn.hpp>

#include "sm/launcher/runtimes/config.hpp"
#include "sm/launcher/runtimes/imagemanager.hpp"

namespace aos::sm::launcher::rootfs {

/**
 * Rootfs runtime.
 */
class RootfsRuntime : public RuntimeItf {
public:
    /**
     * Initializes rootfs runtime.
     *
     * @param config runtime config.
     * @param imageManager image manager.
     * @param ociSpec OCI spec.
     * @param statusReceiver instance status receiver.
     * @param updateChecker update checker.
     * @param rebooter rebooter.
     * @return Error.
     */
    Error Init(const RootfsConfig& config, iamclient::CurrentNodeInfoProviderItf& currentNodeInfoProvider,
        ImageManagerItf& imageManager, oci::OCISpecItf& ociSpec, InstanceStatusReceiverItf& statusReceiver,
        UpdateCheckerItf& updateChecker, RebooterItf& rebooter);

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

    /**
     * Action type type.
     */
    class ActionTypeType {
    public:
        enum class Enum {
            eDoUpdate,
            eDoApply,
            eUpdated,
            eFailed,
            eNumActions,
        };

        static const Array<const char* const> GetStrings()
        {
            static const char* const sStrings[] = {
                "do_update",
                "do_apply",
                "updated",
                "failed",
                "",
            };

            return Array<const char* const>(sStrings, ArraySize(sStrings));
        };
    };

    using ActionTypeEnum = ActionTypeType::Enum;
    using ActionType     = EnumStringer<ActionTypeType>;

    static constexpr auto cResetAllActionFiles = ActionTypeEnum::eNumActions;

    Error      InitCurrentData();
    Error      SetRuntimeID();
    Error      ProcessUpdateAction(InstanceStatus& status);
    Error      ProcessDoUpdate(InstanceStatus& status);
    Error      ProcessUpdated(InstanceStatus& status);
    Error      ProcessFailed(InstanceStatus& status);
    void       FillInstanceStatus(InstanceStatus& status) const;
    Error      SaveInstanceInfo(const InstanceInfo& instance, const std::string& path);
    Error      LoadInstanceInfo(const std::string& path, InstanceInfo& instance);
    Error      GetImageManifest(const String& digest, oci::ImageManifest& manifest) const;
    Error      UnpackImage(const oci::ImageManifest& manifest) const;
    Error      PrepareUpdateFileContent(const oci::ImageManifest& manifest, std::string& updateType) const;
    void       ClearUpdateArtifacts() const;
    Error      StoreAction(ActionTypeEnum action, const std::string& data = "") const;
    ActionType ReadAction() const;
    Error      PrepareUpdate(const InstanceInfo& instance);

    RootfsConfig                           mConfig;
    iamclient::CurrentNodeInfoProviderItf* mCurrentNodeInfoProvider {};
    ImageManagerItf*                       mImageManager {};
    oci::OCISpecItf*                       mOCISpec {};
    InstanceStatusReceiverItf*             mStatusReceiver {};
    UpdateCheckerItf*                      mUpdateChecker {};
    RebooterItf*                           mRebooter {};

    mutable std::mutex        mMutex;
    bool                      mUpdateInProgress {};
    InstanceInfo              mCurrentInstance;
    StaticString<cVersionLen> mCurrentVersion;
};

} // namespace aos::sm::launcher::rootfs

#endif
