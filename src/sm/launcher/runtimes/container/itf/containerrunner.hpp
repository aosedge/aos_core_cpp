/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_CONTAINER_ITF_CONTAINERRUNNER_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_CONTAINER_ITF_CONTAINERRUNNER_HPP_

#include <string>
#include <vector>

#include <core/common/tools/error.hpp>
#include <core/common/types/common.hpp>

namespace aos::sm::launcher {

/**
 * Container status.
 */
struct ContainerStatus {
    std::string       mInstanceID;
    InstanceState     mState;
    Optional<int32_t> mExitCode;
};

/**
 * Container runner interface.
 */
class ContainerRunnerItf {
public:
    /**
     * Destructor.
     */
    virtual ~ContainerRunnerItf() = default;

    /**
     * Starts a container for the given instance.
     *
     * @param instanceID instance ID.
     * @return Error.
     */
    virtual Error StartContainer(const std::string& instanceID) = 0;

    /**
     * Returns the status of a container.
     *
     * @param instanceID instance ID.
     * @return RetWithError<ContainerStatus>.
     */
    virtual RetWithError<ContainerStatus> GetContainerStatus(const std::string& instanceID) = 0;

    /**
     * Returns the status of all managed containers.
     *
     * @return RetWithError<std::vector<ContainerStatus>>.
     */
    virtual RetWithError<std::vector<ContainerStatus>> ListContainers() = 0;

    /**
     * Stops a container.
     *
     * @param instanceID instance ID.
     * @return Error.
     */
    virtual Error StopContainer(const std::string& instanceID) = 0;

    /**
     * Removes a stopped container.
     *
     * @param instanceID instance ID.
     * @return Error.
     */
    virtual Error RemoveContainer(const std::string& instanceID) = 0;
};

} // namespace aos::sm::launcher

#endif
