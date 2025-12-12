/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_ROOTFS_ROOTFS_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_ROOTFS_ROOTFS_HPP_

#include <mutex>
#include <string>

#include <core/common/ocispec/itf/ocispec.hpp>
#include <core/common/tools/time.hpp>
#include <core/sm/launcher/itf/instancestatusreceiver.hpp>
#include <core/sm/launcher/itf/rebooter.hpp>
#include <core/sm/launcher/itf/runtime.hpp>
#include <core/sm/launcher/itf/updatechecker.hpp>
#include <sm/utils/itf/systemdconn.hpp>

#include "config.hpp"

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
     * @param ociSpec OCI spec.
     * @param statusReceiver instance status receiver.
     * @param updateChecker update checker.
     * @param rebooter rebooter.
     * @return Error.
     */
    Error Init(const Config& config, oci::OCISpecItf& ociSpec, InstanceStatusReceiverItf& statusReceiver,
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
    static constexpr auto cImageExtension = ".squashfs";

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
    Error      SaveInstanceInfo(const InstanceInfo& instance, const std::string& path);
    Error      LoadInstanceInfo(const std::string& path, InstanceInfo& instance);
    Error      GetRootfsImagePath(const String& digest, String& path) const;
    Error      CopyRootfsImage(const String& srcPath) const;
    void       ClearUpdateArtifacts() const;
    Error      StoreAction(ActionTypeEnum action) const;
    ActionType ReadAction() const;
    Error      PrepareUpdate(const InstanceInfo& instance);
    ActionType GetCurrentAction();

    Config                     mConfig;
    oci::OCISpecItf*           mOCISpec {};
    InstanceStatusReceiverItf* mStatusReceiver {};
    UpdateCheckerItf*          mUpdateChecker {};
    RebooterItf*               mRebooter {};

    mutable std::mutex        mMutex;
    bool                      mUpdateInProgress {};
    InstanceInfo              mCurrentInstance;
    StaticString<cVersionLen> mCurrentVersion;
};

} // namespace aos::sm::launcher::rootfs

#endif
