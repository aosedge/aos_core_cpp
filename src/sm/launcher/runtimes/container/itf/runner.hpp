/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_CONTAINER_ITF_RUNNER_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_CONTAINER_ITF_RUNNER_HPP_

#include <string>

#include <core/common/types/instance.hpp>

namespace aos::sm::launcher {

/**
 * Instance run status.
 */
struct RunStatus {
    std::string   mInstanceID;
    InstanceState mState;
    Error         mError;

    /**
     * Compares run statuses.
     *
     * @param other run status to compare.
     * @return bool.
     */
    bool operator==(const RunStatus& other) const
    {
        return mInstanceID == other.mInstanceID && mState == other.mState && mError == other.mError;
    }

    /**
     * Compares run statuses.
     *
     * @param other run status to compare.
     * @return bool.
     */
    bool operator!=(const RunStatus& other) const { return !(*this == other); }
};

/**
 * Instance run status receiver interface.
 */
class RunStatusReceiverItf {
public:
    /**
     * Destructs run status receiver interface.
     */
    virtual ~RunStatusReceiverItf() = default;

    /**
     * Updates run instances status.
     *
     * @param instances instances state.
     * @return Error.
     */
    virtual Error UpdateRunStatus(const std::vector<RunStatus>& instances) = 0;
};

/**
 * Runner interface.
 */
class RunnerItf {
public:
    /**
     * Destructor.
     */
    virtual ~RunnerItf() = default;

    /**
     * Initializes runner.
     *
     * @param receiver run status receiver.
     * @return Error.
     */
    virtual Error Init(RunStatusReceiverItf& receiver) = 0;

    /**
     * Starts runner.
     *
     * @return Error.
     */
    virtual Error Start() = 0;

    /**
     * Stops runner.
     *
     * @return Error.
     */
    virtual Error Stop() = 0;

    /**
     * Starts instance.
     *
     * @param instanceID instance ID.
     * @param runParams runtime parameters.
     * @return RunStatus.
     */
    virtual RunStatus StartInstance(const std::string& instanceID, const RunParameters& runParams) = 0;

    /**
     * Stops instance.
     *
     * @param instanceID instance ID.
     * @return Error.
     */
    virtual Error StopInstance(const std::string& instanceID) = 0;
};

} // namespace aos::sm::launcher

#endif
