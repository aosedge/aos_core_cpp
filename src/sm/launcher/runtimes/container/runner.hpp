/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_CONTAINER_RUNNER_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_CONTAINER_RUNNER_HPP_

#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <core/common/tools/time.hpp>

#include <sm/utils/itf/systemdconn.hpp>

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
     * @param systemdConn systemd connection.
     * @return Error.
     */
    Error Init(RunStatusReceiverItf& receiver, sm::utils::SystemdConnItf& systemdConn) override;

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
     * @param runParams runtime parameters.
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
    static constexpr auto cDefaultStopTimeout     = 5 * Time::cSeconds;
    static constexpr auto cStartTimeMultiplier    = 1.2;
    static constexpr auto cDefaultStartBurst      = 3;
    static constexpr auto cDefaultRestartInterval = 1 * Time::cSeconds;

    static constexpr auto cStatusPollPeriod = std::chrono::seconds(1);

    static constexpr auto cSystemdUnitNameTemplate = "aos-service@%s.service";
    static constexpr auto cSystemdDropInsDir       = "/run/systemd/system";
    static constexpr auto cParametersFileName      = "parameters.conf";

    virtual std::string GetSystemdDropInsDir() const;

    void                        MonitorUnits();
    std::vector<RunStatus>&     GetRunningInstances() const;
    Error                       SetRunParameters(const std::string& unitName, const RunParameters& params);
    Error                       RemoveRunParameters(const std::string& unitName);
    RetWithError<InstanceState> GetStartingUnitState(const std::string& unitName, Duration startInterval);

    static std::string CreateSystemdUnitName(const std::string& instance);
    static std::string CreateInstanceID(const std::string& unitname);

    struct StartingUnitData {
        std::condition_variable mCondVar;
        sm::utils::UnitState    mRunState;
        Optional<int32_t>       mExitCode;
    };

    struct RunningUnitData {
        InstanceState     mRunState;
        Optional<int32_t> mExitCode;
    };

    RunStatusReceiverItf* mRunStatusReceiver = nullptr;

    sm::utils::SystemdConnItf* mSystemd = {};
    std::thread                mMonitoringThread;
    std::mutex                 mMutex;
    std::condition_variable    mCondVar;

    std::map<std::string, StartingUnitData> mStartingUnits;
    std::map<std::string, RunningUnitData>  mRunningUnits;
    mutable std::vector<RunStatus>          mRunningInstances;

    bool mClosed = false;
};

} // namespace aos::sm::launcher

#endif
