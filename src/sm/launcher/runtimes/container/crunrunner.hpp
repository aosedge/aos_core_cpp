/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_CONTAINER_CRUNRUNNER_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_CONTAINER_CRUNRUNNER_HPP_

#include <string>

#include "config.hpp"
#include "itf/containerrunner.hpp"

namespace aos::sm::launcher {

/**
 * crun container runner.
 *
 * Containers are created through conmon, which stays alive as the container monitor (subreaper) for the container's
 * whole lifetime and records its exit code and OOM kill into a per-instance persist directory under the conmon dir.
 */
class CRunRunner : public ContainerRunnerItf {
public:
    /**
     * Initializes the crun runner.
     *
     * @param config container runtime config.
     * @return Error.
     */
    Error Init(const ContainerConfig& config);

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
     * Returns the status of every container found under the crun state root (--root) given to Init().
     *
     * That directory must be exclusively ours (see ListContainers() in crunrunner.cpp), so this is
     * equivalent to "all containers this runner manages", including ones started by a previous
     * process instance before a restart - not just ones started via StartContainer() in this
     * object's lifetime.
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
    RetWithError<ContainerStatus> GetStatus(const std::string& instanceID) const;
    void                          ReadExitStatus(const std::string& instanceID, ContainerStatus& status) const;
    Error                         CreateContainer(const std::string& instanceID);
    void                          WaitConmonExit(const std::string& instanceID) const;
    std::string                   GetPersistDir(const std::string& instanceID) const;

    std::string mRuntimeDir;
    std::string mStateRoot;
    std::string mCRunExecutable;
    std::string mConmonExecutable;
    std::string mConmonDir;
};

} // namespace aos::sm::launcher

#endif
