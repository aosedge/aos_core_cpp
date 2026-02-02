/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_CONTAINER_CONTAINER_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_CONTAINER_CONTAINER_HPP_

#include <condition_variable>
#include <mutex>
#include <unordered_map>

#include <core/common/iamclient/itf/currentnodeinfoprovider.hpp>
#include <core/sm/launcher/itf/instancestatusreceiver.hpp>
#include <core/sm/launcher/itf/runtime.hpp>

#include <common/utils/utils.hpp>
#include <sm/alerts/itf/instanceinfoprovider.hpp>
#include <sm/launcher/runtimes/config.hpp>
#include <sm/logprovider/itf/instanceidprovider.hpp>

#include "instance.hpp"

namespace aos::sm::launcher {

/**
 * Container runtime name.
 */
constexpr auto cRuntimeContainer = "container";

/**
 * Container runtime implementation.
 */
class ContainerRuntime : public RuntimeItf,
                         public RunStatusReceiverItf,
                         public alerts::InstanceInfoProviderItf,
                         public logprovider::InstanceIDProviderItf {
public:
    /**
     * Initializes container runtime.
     *
     * @param config runtime config.
     * @param currentNodeInfoProvider current node info provider.
     * @param itemInfoProvider item info provider.
     * @param networkManager network manager.
     * @param permHandler permission handler.
     * @param resourceInfoProvider resource info provider.
     * @param ociSpec OCI spec interface.
     * @param instanceStatusReceiver instance status receiver.
     * @param systemdConn systemd connection.
     * @return Error.
     */
    Error Init(const RuntimeConfig& config, aos::iamclient::CurrentNodeInfoProviderItf& currentNodeInfoProvider,
        imagemanager::ItemInfoProviderItf& itemInfoProvider, networkmanager::NetworkManagerItf& networkManager,
        aos::iamclient::PermHandlerItf& permHandler, resourcemanager::ResourceInfoProviderItf& resourceInfoProvider,
        oci::OCISpecItf& ociSpec, InstanceStatusReceiverItf& instanceStatusReceiver,
        sm::utils::SystemdConnItf& systemdConn);

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
     * @param instanceInfo instance to start.
     * @param[out] status instance status.
     * @return Error.
     */
    Error StartInstance(const InstanceInfo& instanceInfo, InstanceStatus& status) override;

    /**
     * Stop instance.
     *
     * @param instanceIdent instance to stop.
     * @param[out] status instance status.
     * @return Error.
     */
    Error StopInstance(const InstanceIdent& instanceIdent, InstanceStatus& status) override;

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

    /**
     * Returns service instance info.
     *
     * @param instanceID instance id.
     * @param[out] instanceInfo instance info.
     * @return Error.
     */
    Error GetInstanceInfoByID(const String& instanceID, alerts::InstanceInfo& instanceInfo) override;

    /**
     * Returns service instance IDs.
     *
     * @param filter log filter.
     * @param[out] instanceIDs instance IDs.
     * @return Error.
     */
    Error GetInstanceIDs(const LogFilter& filter, std::vector<std::string>& instanceIDs) override;

private:
    virtual std::shared_ptr<RunnerItf>     CreateRunner();
    virtual std::shared_ptr<FileSystemItf> CreateFileSystem();
    virtual std::shared_ptr<MonitoringItf> CreateMonitoring();

    Error UpdateRunStatus(const std::vector<RunStatus>& instances) override;

    Error CreateRuntimeInfo(const std::string& runtimeType, const NodeInfo& nodeInfo);
    Error StopActiveInstances();
    void  SendInstanceStatus(const InstanceStatus& status);

    std::shared_ptr<RunnerItf>     mRunner;
    std::shared_ptr<FileSystemItf> mFileSystem;
    std::shared_ptr<MonitoringItf> mMonitoring;

    imagemanager::ItemInfoProviderItf*        mItemInfoProvider {};
    networkmanager::NetworkManagerItf*        mNetworkManager {};
    aos::iamclient::PermHandlerItf*           mPermHandler {};
    resourcemanager::ResourceInfoProviderItf* mResourceInfoProvider {};
    oci::OCISpecItf*                          mOCISpec {};
    InstanceStatusReceiverItf*                mInstanceStatusReceiver {};

    ContainerConfig                                              mConfig;
    NodeInfo                                                     mNodeInfo;
    RuntimeInfo                                                  mRuntimeInfo;
    std::mutex                                                   mMutex;
    std::condition_variable                                      mCV;
    std::unordered_map<InstanceIdent, std::shared_ptr<Instance>> mCurrentInstances;
};

} // namespace aos::sm::launcher

#endif
