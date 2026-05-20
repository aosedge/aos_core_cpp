/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_CONTAINER_RUNNER_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_CONTAINER_RUNNER_HPP_

#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <core/common/tools/time.hpp>

#include "itf/runner.hpp"

namespace aos::sm::launcher {

/**
 * Service runner.
 */
class Runner : public RunnerItf {
public:
    /**
     * Initializes Runner instance.
     *
     * @param receiver run status receiver.
     * @param containerRunner container runner.
     * @return Error.
     */
    Error Init(RunStatusReceiverItf& receiver, ContainerRunnerItf& containerRunner) override;

    /**
     * Starts monitoring thread.
     *
     * @return Error.
     */
    Error Start() override;

    /**
     * Stops Runner.
     *
     * @return Error.
     */
    Error Stop() override;

    /**
     * Starts service instance.
     *
     * @param instanceID instance ID.
     * @param params runtime parameters.
     * @return RunStatus.
     */
    RunStatus StartInstance(const std::string& instanceID, const RunParameters& params) override;

    /**
     * Stops service instance.
     *
     * @param instanceID instance ID.
     * @return Error.
     */
    Error StopInstance(const std::string& instanceID) override;

private:
    static constexpr auto cDefaultStartInterval   = 5 * Time::cSeconds;
    static constexpr auto cDefaultStartBurst      = 3;
    static constexpr auto cDefaultRestartInterval = 1 * Time::cSeconds;
    static constexpr auto cStatusPollPeriod       = std::chrono::seconds(1);

    bool                        SyncStates();
    std::vector<std::string>    GetInstancesToRestart();
    void                        MonitorContainers();
    std::vector<RunStatus>&     GetRunningInstances() const;
    RetWithError<InstanceState> InitContainerState(const std::string& instanceID, const RunParameters& params);

    struct RunningUnitData {
        InstanceState       mRunState;
        RunParameters       mParams;
        Time                mFirstStartTime = {Time::Now()};
        std::optional<Time> mNextRestartAt;
        int                 mRestartCount      = {};
        bool                mExceedsBurstLimit = {};
    };

    RunStatusReceiverItf*   mRunStatusReceiver = {};
    ContainerRunnerItf*     mContainerRunner   = {};
    std::thread             mMonitoringThread;
    std::mutex              mMutex;
    std::condition_variable mCondVar;

    std::unordered_map<std::string, RunningUnitData> mRunningContainers;
    mutable std::vector<RunStatus>                   mRunningInstances;

    bool mClosed = false;
};

} // namespace aos::sm::launcher

#endif
