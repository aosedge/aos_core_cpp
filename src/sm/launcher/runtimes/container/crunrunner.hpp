/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_CONTAINER_CRUNRUNNER_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_CONTAINER_CRUNRUNNER_HPP_

#include <mutex>
#include <set>
#include <string>

#include "itf/containerrunner.hpp"

namespace aos::sm::launcher {

/**
 * crun container runner.
 */
class CRunRunner : public ContainerRunnerItf {
public:
    /**
     * Initializes the crun runner.
     *
     * @param runtimeDir base directory for per-instance runtime files.
     * @return Error.
     */
    Error Init(const std::string& runtimeDir);
    /**
     * Starts a container for the given instance.
     *
     * @param instanceID instance ID.
     * @return Error.
     */
    Error StartContainer(const std::string& instanceID) override;

    /**
     * Returns the status of a container.
     *
     * @param instanceID instance ID.
     * @return RetWithError<ContainerStatus>.
     */
    RetWithError<ContainerStatus> GetContainerStatus(const std::string& instanceID) override;

    /**
     * Returns the status of all managed containers.
     *
     * @return RetWithError<std::vector<ContainerStatus>>.
     */
    RetWithError<std::vector<ContainerStatus>> ListContainers() override;

    /**
     * Stops a container.
     *
     * @param instanceID instance ID.
     * @return Error.
     */
    Error StopContainer(const std::string& instanceID) override;

    /**
     * Removes a stopped container.
     *
     * @param instanceID instance ID.
     * @return Error.
     */
    Error RemoveContainer(const std::string& instanceID) override;

private:
    static constexpr auto cStateRoot      = "/run/crun";
    static constexpr auto cCRunExecutable = "/usr/bin/crun";

    RetWithError<ContainerStatus> CheckProcessAlive(const std::string& instanceID) const;

    std::string           mRuntimeDir;
    std::mutex            mMutex;
    std::set<std::string> mManagedInstances;
};

} // namespace aos::sm::launcher

#endif
