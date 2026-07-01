/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <algorithm>
#include <filesystem>

#include <core/common/tools/logger.hpp>

#include "runner.hpp"

namespace aos::sm::launcher {

/***********************************************************************************************************************
 * Statics
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error Runner::Init(RunStatusReceiverItf& receiver, ContainerRunnerItf& containerRunner)
{
    mRunStatusReceiver = &receiver;
    mContainerRunner   = &containerRunner;

    return ErrorEnum::eNone;
}

Error Runner::Start()
{
    LOG_DBG() << "Start runner";

    mClosed           = false;
    mMonitoringThread = std::thread(&Runner::MonitorContainers, this);

    return ErrorEnum::eNone;
}

Error Runner::Stop()
{
    {
        std::lock_guard lock {mMutex};

        if (mClosed) {
            return ErrorEnum::eNone;
        }

        LOG_DBG() << "Stop runner";

        mClosed = true;
        mCondVar.notify_all();
    }

    if (mMonitoringThread.joinable()) {
        mMonitoringThread.join();
    }

    return ErrorEnum::eNone;
}

RunStatus Runner::StartInstance(const std::string& instanceID, const RunParameters& params)
{
    RunStatus status = {};

    status.mInstanceID = instanceID;
    status.mState      = InstanceStateEnum::eFailed;

    RunParameters fixedParams = params;

    if (!params.mStartInterval.HasValue()) {
        fixedParams.mStartInterval = cDefaultStartInterval;
    }

    if (!params.mStartBurst.HasValue()) {
        fixedParams.mStartBurst = cDefaultStartBurst;
    }

    if (!params.mRestartInterval.HasValue()) {
        fixedParams.mRestartInterval = cDefaultRestartInterval;
    }

    LOG_DBG() << "Start service instance" << Log::Field("instanceID", instanceID.c_str())
              << Log::Field("startInterval", fixedParams.mStartInterval)
              << Log::Field("startBurst", fixedParams.mStartBurst)
              << Log::Field("restartInterval", fixedParams.mRestartInterval);

    if (status.mError = mContainerRunner->StartContainer(instanceID); !status.mError.IsNone()) {
        return status;
    }

    // Get unit status.
    Tie(status.mState, status.mError) = InitContainerState(instanceID, fixedParams);

    LOG_DBG() << "Start instance" << Log::Field("instanceID", instanceID.c_str()) << Log::Field("state", status.mState)
              << Log::Field("error", status.mError);

    return status;
}

Error Runner::StopInstance(const std::string& instanceID)
{
    LOG_DBG() << "Stop instance" << Log::Field("instanceID", instanceID.c_str());

    {
        std::lock_guard lock {mMutex};

        mRunningContainers.erase(instanceID);
    }

    auto err = mContainerRunner->StopContainer(instanceID);
    if (!err.IsNone()) {
        if (err.Is(ErrorEnum::eNotFound)) {
            LOG_DBG() << "Process not found" << Log::Field("instanceID", instanceID.c_str());

            err = ErrorEnum::eNone;
        }
    }

    if (auto removeErr = mContainerRunner->RemoveContainer(instanceID); !removeErr.IsNone()) {
        if (!removeErr.Is(ErrorEnum::eNotFound) && err.IsNone()) {
            err = removeErr;
        }
    }

    return err;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

bool Runner::SyncStates()
{
    auto [currentStates, err] = mContainerRunner->ListContainers();
    if (!err.IsNone()) {
        LOG_ERR() << "List containers failed" << Log::Field(err);

        return false;
    }

    bool       stateChanged = false;
    const auto now          = Time::Now();

    for (auto& stored : mRunningContainers) {
        auto currentIt = std::find_if(currentStates.begin(), currentStates.end(),
            [&stored](const ContainerStatus& status) { return status.mInstanceID == stored.first; });

        if (currentIt == currentStates.end()) {
            LOG_WRN() << "Unknown container" << Log::Field("id", stored.first.c_str());

            continue;
        }

        auto&       storedData   = stored.second;
        const auto& currentState = *currentIt;

        if (currentState.mState != storedData.mRunState) {
            stateChanged = true;
        }

        storedData.mRunState = currentState.mState;

        if (currentState.mState == InstanceStateEnum::eActive || storedData.mExceedsBurstLimit) {
            storedData.mNextRestartAt.reset();

            continue;
        }

        if (!storedData.mNextRestartAt) {
            const auto restartMs = storedData.mParams.mRestartInterval.GetValue().Milliseconds();

            storedData.mNextRestartAt = now.Add(restartMs);

            LOG_DBG() << "Container is not active, scheduling restart"
                      << Log::Field("instanceID", currentState.mInstanceID.c_str())
                      << Log::Field("restartInterval", restartMs);
        }
    }

    return stateChanged;
}

std::vector<std::string> Runner::GetInstancesToRestart()
{
    const auto now = Time::Now();

    std::vector<std::string> instances;

    for (auto& [instanceID, runningState] : mRunningContainers) {
        if (!runningState.mNextRestartAt.has_value() || now < *runningState.mNextRestartAt) {
            continue;
        }

        runningState.mNextRestartAt.reset();

        if (now < runningState.mFirstStartTime.Add(*runningState.mParams.mStartInterval)) {
            LOG_DBG() << "Restart burst detected" << Log::Field("instanceID", instanceID.c_str())
                      << Log::Field("restartCount", runningState.mRestartCount);

            if (const auto burst = runningState.mParams.mStartBurst.GetValue();
                burst > 0 && runningState.mRestartCount >= burst) {
                LOG_WRN() << "Restart burst limit exceeded, stopping restart attempts"
                          << Log::Field("instanceID", instanceID.c_str());

                runningState.mExceedsBurstLimit = true;

                continue;
            }

            ++runningState.mRestartCount;
        }

        instances.push_back(instanceID);
    }

    return instances;
}

void Runner::MonitorContainers()
{
    while (!mClosed) {
        std::vector<std::string>              instancesToRestart;
        std::optional<std::vector<RunStatus>> runStatusUpdate;

        {
            std::unique_lock<std::mutex> lock {mMutex};

            if (mCondVar.wait_for(lock, cStatusPollPeriod, [this]() { return mClosed; })) {
                return;
            }

            const auto stateChanged = SyncStates();

            if (stateChanged || mRunningContainers.size() != mRunningInstances.size()) {
                runStatusUpdate = GetRunningInstances();
            }

            instancesToRestart = GetInstancesToRestart();
        }

        if (runStatusUpdate) {
            mRunStatusReceiver->UpdateRunStatus(*runStatusUpdate);
        }

        for (const auto& instanceID : instancesToRestart) {
            LOG_DBG() << "Restarting container" << Log::Field("instanceID", instanceID.c_str());

            if (auto err = mContainerRunner->RemoveContainer(instanceID); !err.IsNone()) {
                LOG_WRN() << "Failed to remove container" << Log::Field("instanceID", instanceID.c_str())
                          << Log::Field(err);
            }

            if (auto err = mContainerRunner->StartContainer(instanceID); !err.IsNone()) {
                LOG_ERR() << "Failed to restart container" << Log::Field("instanceID", instanceID.c_str())
                          << Log::Field(err);
            }
        }
    }
}

std::vector<RunStatus>& Runner::GetRunningInstances() const
{
    mRunningInstances.clear();

    std::transform(mRunningContainers.begin(), mRunningContainers.end(), std::back_inserter(mRunningInstances),
        [](const auto& unit) {
            return RunStatus {unit.first, unit.second.mRunState, {}};
        });

    return mRunningInstances;
}

RetWithError<InstanceState> Runner::InitContainerState(const std::string& instanceID, const RunParameters& params)
{
    auto [status, err] = mContainerRunner->GetContainerStatus(instanceID);
    if (!err.IsNone()) {
        return {InstanceStateEnum::eFailed, AOS_ERROR_WRAP(Error(err, "failed to get container status"))};
    }

    {
        std::unique_lock lock {mMutex};

        auto& runningUnit     = mRunningContainers[instanceID];
        runningUnit.mRunState = status.mState;
        runningUnit.mParams   = params;
    }

    return status.mState;
}

} // namespace aos::sm::launcher
