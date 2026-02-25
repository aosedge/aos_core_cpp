/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <algorithm>
#include <filesystem>

#include <Poco/Format.h>
#include <Poco/String.h>

#include <core/common/tools/fs.hpp>
#include <core/common/tools/logger.hpp>
#include <core/common/types/common.hpp>

#include <common/utils/exception.hpp>
#include <sm/utils/systemdconn.hpp>

#include "runner.hpp"

namespace aos::sm::launcher {

/***********************************************************************************************************************
 * Statics
 **********************************************************************************************************************/

namespace {

inline InstanceState ToInstanceState(utils::UnitState state)
{
    switch (state.GetValue()) {
    case utils::UnitStateEnum::eActive:
        return InstanceStateEnum::eActive;

    case utils::UnitStateEnum::eInactive:
        return InstanceStateEnum::eInactive;

    default:
        return InstanceStateEnum::eFailed;
    }
}

Error CreateDir(const std::string& path, unsigned perms)
{
    std::error_code code;

    std::filesystem::create_directories(path, code);
    if (code.value() != 0) {
        return AOS_ERROR_WRAP(Error(code.value(), code.message().c_str()));
    }

    std::filesystem::permissions(
        path, static_cast<std::filesystem::perms>(perms), std::filesystem::perm_options::replace, code);

    if (code.value() != 0) {
        return AOS_ERROR_WRAP(Error(code.value(), code.message().c_str()));
    }

    return ErrorEnum::eNone;
}

} // namespace

/***********************************************************************************************************************
 * Implementation
 **********************************************************************************************************************/

Error Runner::Init(RunStatusReceiverItf& listener, utils::SystemdConnItf& systemdConn)
{
    mRunStatusReceiver = &listener;
    mSystemd           = &systemdConn;

    return ErrorEnum::eNone;
}

Error Runner::Start()
{
    LOG_DBG() << "Start runner";

    mClosed           = false;
    mMonitoringThread = std::thread(&Runner::MonitorUnits, this);

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

    // Fix run parameters.
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

    // Create systemd service file.
    const auto unitName = CreateSystemdUnitName(instanceID);

    if (status.mError = SetRunParameters(unitName, fixedParams); !status.mError.IsNone()) {
        return status;
    }

    // Start unit.
    const auto startTime = static_cast<Duration>(cStartTimeMultiplier * fixedParams.mStartInterval.GetValue());

    if (status.mError = mSystemd->StartUnit(unitName, "replace", startTime); !status.mError.IsNone()) {
        return status;
    }

    // Get unit status.
    Tie(status.mState, status.mError) = GetStartingUnitState(unitName, startTime);

    LOG_DBG() << "Start instance" << Log::Field("instanceID", instanceID.c_str())
              << Log::Field("name", unitName.c_str()) << Log::Field("state", status.mState)
              << Log::Field("error", status.mError);

    return status;
}

Error Runner::StopInstance(const std::string& instanceID)
{
    LOG_DBG() << "Stop instance" << Log::Field("instanceID", instanceID.c_str());

    const auto unitName = CreateSystemdUnitName(instanceID);

    {
        std::lock_guard lock {mMutex};

        mRunningUnits.erase(unitName);
    }

    auto err = mSystemd->StopUnit(unitName, "replace", cDefaultStopTimeout);
    if (!err.IsNone()) {
        if (err.Is(ErrorEnum::eNotFound)) {
            LOG_DBG() << "Service not loaded" << Log::Field("instanceID", instanceID.c_str());

            err = ErrorEnum::eNone;
        }
    }

    if (auto releaseErr = mSystemd->ResetFailedUnit(unitName); !releaseErr.IsNone()) {
        if (!releaseErr.Is(ErrorEnum::eNotFound) && err.IsNone()) {
            err = releaseErr;
        }
    }

    if (auto rmErr = RemoveRunParameters(unitName); !rmErr.IsNone()) {
        if (err.IsNone()) {
            err = rmErr;
        }
    }

    return err;
}

std::string Runner::GetSystemdDropInsDir() const
{
    return cSystemdDropInsDir;
}

void Runner::MonitorUnits()
{
    while (!mClosed) {
        std::unique_lock lock {mMutex};

        bool closed = mCondVar.wait_for(lock, cStatusPollPeriod, [this]() { return mClosed; });
        if (closed) {
            return;
        }

        auto [units, err] = mSystemd->ListUnits();
        if (!err.IsNone()) {
            LOG_ERR() << "Systemd list units failed" << Log::Field(err);

            return;
        }

        bool unitChanged = false;

        for (const auto& unit : units) {
            // Update starting units
            auto startUnitIt = mStartingUnits.find(unit.mName);
            if (startUnitIt != mStartingUnits.end()) {
                startUnitIt->second.mRunState = unit.mActiveState;
                startUnitIt->second.mExitCode = unit.mExitCode;

                LOG_DBG() << "==== Unit state updated" << Log::Field("unit", unit.mName.c_str())
                          << Log::Field("state", unit.mActiveState) << Log::Field("exitCode", unit.mExitCode);

                // systemd doesn't change the state of failed unit => notify listener about final state.
                if (unit.mActiveState == utils::UnitStateEnum::eFailed) {
                    startUnitIt->second.mCondVar.notify_all();
                }
            }

            // Update running units
            auto runUnitIt = mRunningUnits.find(unit.mName);
            if (runUnitIt != mRunningUnits.end()) {
                auto& runningState  = runUnitIt->second;
                auto  instanceState = ToInstanceState(unit.mActiveState);

                if (instanceState != runningState.mRunState || unit.mExitCode != runningState.mExitCode) {
                    runningState = RunningUnitData {instanceState, unit.mExitCode};
                    unitChanged  = true;
                }
            }
        }

        if (unitChanged || mRunningUnits.size() != mRunningInstances.size()) {
            mRunStatusReceiver->UpdateRunStatus(GetRunningInstances());
        }
    }
}

std::vector<RunStatus>& Runner::GetRunningInstances() const
{
    mRunningInstances.clear();

    std::transform(
        mRunningUnits.begin(), mRunningUnits.end(), std::back_inserter(mRunningInstances), [](const auto& unit) {
            const auto instanceID = CreateInstanceID(unit.first);

            auto error = unit.second.mExitCode.HasValue() ? Error(unit.second.mExitCode.GetValue()) : Error();

            return RunStatus {instanceID, unit.second.mRunState, error};
        });

    return mRunningInstances;
}

Error Runner::SetRunParameters(const std::string& unitName, const RunParameters& params)
{
    const std::string parametersFormat = "[Unit]\n"
                                         "StartLimitIntervalSec=%us\n"
                                         "StartLimitBurst=%ld\n\n"
                                         "[Service]\n"
                                         "RestartSec=%us\n";

    std::string formattedContent
        = Poco::format(parametersFormat, static_cast<uint32_t>(params.mStartInterval->Seconds()), *params.mStartBurst,
            static_cast<uint32_t>(params.mRestartInterval->Seconds()));

    const std::string parametersDir = GetSystemdDropInsDir() + "/" + unitName + ".d";

    if (auto err = CreateDir(parametersDir, 0755U); !err.IsNone()) {
        return err;
    }

    const auto paramsFile = parametersDir + "/" + cParametersFileName;

    return fs::WriteStringToFile(paramsFile.c_str(), formattedContent.c_str(), 0644U);
}

Error Runner::RemoveRunParameters(const std::string& unitName)
{
    const std::string parametersDir = GetSystemdDropInsDir() + "/" + unitName + ".d";

    return fs::RemoveAll(parametersDir.c_str());
}

RetWithError<InstanceState> Runner::GetStartingUnitState(const std::string& unitName, Duration startInterval)
{
    const auto timeout = std::chrono::milliseconds(startInterval.Milliseconds());

    auto [initialStatus, err] = mSystemd->GetUnitStatus(unitName);
    if (!err.IsNone()) {
        return {InstanceStateEnum::eFailed, AOS_ERROR_WRAP(Error(err, "failed to get unit status"))};
    }

    {
        std::unique_lock lock {mMutex};

        mStartingUnits[unitName].mRunState = initialStatus.mActiveState;
        mStartingUnits[unitName].mExitCode = initialStatus.mExitCode;

        // Wait specified duration for unit state updates.
        std::ignore   = mStartingUnits[unitName].mCondVar.wait_for(lock, timeout);
        auto runState = mStartingUnits[unitName].mRunState;
        auto exitCode
            = mStartingUnits[unitName].mExitCode.HasValue() ? mStartingUnits[unitName].mExitCode.GetValue() : 0;

        mStartingUnits.erase(unitName);

        if (runState.GetValue() != utils::UnitStateEnum::eActive) {
            const auto errMsg = "failed to start unit";
            err               = (exitCode) ? Error(exitCode, errMsg) : Error(ErrorEnum::eFailed, errMsg);

            return {InstanceStateEnum::eFailed, AOS_ERROR_WRAP(err)};
        }

        mRunningUnits[unitName] = RunningUnitData {InstanceStateEnum::eActive, exitCode};

        return {InstanceStateEnum::eActive, ErrorEnum::eNone};
    }
}

std::string Runner::CreateSystemdUnitName(const std::string& instance)
{
    return Poco::format(cSystemdUnitNameTemplate, instance);
}

std::string Runner::CreateInstanceID(const std::string& unitname)
{
    const std::string prefix = "aos-service@";
    const std::string suffix = ".service";

    if (Poco::startsWith(unitname, prefix) && Poco::endsWith(unitname, suffix)) {
        return unitname.substr(prefix.length(), unitname.length() - prefix.length() - suffix.length());
    } else {
        AOS_ERROR_THROW(AOS_ERROR_WRAP(Error(ErrorEnum::eInvalidArgument)), "not a valid Aos service name");
    }
}

} // namespace aos::sm::launcher
