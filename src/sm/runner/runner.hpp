/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_RUNNER_RUNNER_HPP_
#define AOS_SM_RUNNER_RUNNER_HPP_

#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <core/common/tools/time.hpp>
#include <core/common/types/obsolete.hpp>

#include <sm/utils/systemdconn.hpp>

namespace aos::sm::runner {

/**
 * Service runner.
 */
class Runner : public RunnerItf {
public:
    /**
     * Initializes Runner instance.
     *
     * @param receiver run status receiver.
     * @return Error.
     */
    Error Init(RunStatusReceiverItf& receiver);

    /**
     * Starts monitoring thread.
     *
     * @return Error.
     */
    Error Start();

    /**
     * Stops Runner.
     *
     * @return Error.
     */
    Error Stop();

    /**
     * Destructor.
     */
    ~Runner();

    /**
     * Starts service instance.
     *
     * @param instanceID instance ID.
     * @param runtimeDir directory with runtime spec.
     * @param runParams runtime parameters.
     * @return RunStatus.
     */
    RunStatus StartInstance(const String& instanceID, const String& runtimeDir, const RunParameters& params) override;

    /**
     * Stops service instance.
     *
     * @param instanceID instance ID
     * @return Error.
     */
    Error StopInstance(const String& instanceID) override;

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

    virtual std::shared_ptr<utils::SystemdConnItf> CreateSystemdConn();
    virtual std::string                            GetSystemdDropInsDir() const;

    void                        MonitorUnits();
    Array<RunStatus>            GetRunningInstances() const;
    Error                       SetRunParameters(const std::string& unitName, const RunParameters& params);
    Error                       RemoveRunParameters(const std::string& unitName);
    RetWithError<InstanceState> GetStartingUnitState(const std::string& unitName, Duration startInterval);

    static std::string CreateSystemdUnitName(const String& instance);
    static std::string CreateInstanceID(const std::string& unitname);

    struct StartingUnitData {
        std::condition_variable mCondVar;
        UnitState               mRunState;
        Optional<int32_t>       mExitCode;
    };

    struct RunningUnitData {
        InstanceState     mRunState;
        Optional<int32_t> mExitCode;
    };

    RunStatusReceiverItf* mRunStatusReceiver = nullptr;

    std::shared_ptr<utils::SystemdConnItf> mSystemd;
    std::thread                            mMonitoringThread;
    std::mutex                             mMutex;
    std::condition_variable                mCondVar;

    std::map<std::string, StartingUnitData> mStartingUnits;
    std::map<std::string, RunningUnitData>  mRunningUnits;
    mutable std::vector<RunStatus>          mRunningInstances;

    bool mClosed = false;
};

} // namespace aos::sm::runner

#endif // RUNNER_HPP_
