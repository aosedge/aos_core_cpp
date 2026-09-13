/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_CONTAINER_CRUNRUNNER_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_CONTAINER_CRUNRUNNER_HPP_

#include <sys/types.h>

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

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
     * @param stateRoot crun state root directory (--root).
     * @param crunExecutable path to the crun executable.
     * @return Error.
     */
    Error Init(const std::string& runtimeDir, const std::string& stateRoot, const std::string& crunExecutable);
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
    RetWithError<ContainerStatus> CheckProcessAlive(const std::string& instanceID) const;
    void                          ReapExitedContainers();

    // Exit codes are reaped via waitpid() on our direct child, so they only ever exist in this process's
    // memory. Persisting the value to disk the moment it's known lets a restarted SM recover it for
    // GetContainerStatus()/ListContainers() instead of only ever seeing "not running".
    std::string            ExitCodeFilePath(const std::string& instanceID) const;
    void                   PersistExitCode(const std::string& instanceID, int32_t exitCode) const;
    std::optional<int32_t> ReadPersistedExitCode(const std::string& instanceID) const;

    std::string        mRuntimeDir;
    std::string        mStateRoot;
    std::string        mCRunExecutable;
    mutable std::mutex mMutex;

    std::unordered_map<std::string, pid_t>   mPids; // instanceID -> unreaped crun subprocess pid
    std::unordered_map<std::string, int32_t> mExitCodes; // instanceID -> exit code, once reaped
};

} // namespace aos::sm::launcher

#endif
