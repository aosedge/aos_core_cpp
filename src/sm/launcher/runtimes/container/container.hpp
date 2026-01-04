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
#include <core/sm/launcher/itf/runtime.hpp>

#include <common/utils/utils.hpp>
#include <sm/launcher/runtimes/config.hpp>

#include "itf/filesystem.hpp"
#include "itf/runner.hpp"

#include "instance.hpp"

namespace aos::sm::launcher {

/**
 * Container runtime name.
 */
constexpr auto cRuntimeContainer = "container";

/**
 * Container runtime implementation.
 */
class ContainerRuntime : public RuntimeItf, public RunStatusReceiverItf {
public:
    /**
     * Initializes container runtime.
     *
     * @param config runtime config.
     * @param currentNodeInfoProvider current node info provider.
     * @return Error.
     */
    Error Init(const RuntimeConfig& config, iamclient::CurrentNodeInfoProviderItf& currentNodeInfoProvider);

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

private:
    virtual std::shared_ptr<RunnerItf>     CreateRunner();
    virtual std::shared_ptr<FileSystemItf> CreateFileSystem();

    Error UpdateRunStatus(const std::vector<RunStatus>& instances) override;

    Error CreateRuntimeInfo(const std::string& runtimeType, const NodeInfo& nodeInfo);

    std::shared_ptr<RunnerItf>     mRunner;
    std::shared_ptr<FileSystemItf> mFileSystem;

    ContainerConfig                                              mConfig;
    RuntimeInfo                                                  mRuntimeInfo;
    std::mutex                                                   mMutex;
    std::condition_variable                                      mCV;
    std::unordered_map<InstanceIdent, std::shared_ptr<Instance>> mCurrentInstances;
};

} // namespace aos::sm::launcher

#endif
