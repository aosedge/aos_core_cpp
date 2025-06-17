/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <algorithm>
#include <filesystem>

#include <Poco/Format.h>
#include <Poco/String.h>

#include <aos/common/tools/fs.hpp>
#include <logger/logmodule.hpp>
#include <utils/exception.hpp>

#include "runner.hpp"

namespace aos::sm::runner {

/***********************************************************************************************************************
 * Statics
 **********************************************************************************************************************/

namespace {

inline InstanceRunState ToInstanceState(UnitState state)
{
    if (state.GetValue() == UnitStateEnum::eActive) {
        return InstanceRunStateEnum::eActive;
    } else {
        return InstanceRunStateEnum::eFailed;
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

Error Runner::Init(RunStatusReceiverItf& listener)
{
    mRunStatusReceiver = &listener;

    return ErrorEnum::eNone;
}

Error Runner::Start()
{
    LOG_DBG() << "Start runner";

    try {
        mSystemd = CreateSystemdConn();
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

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

    mSystemd.reset();

    return ErrorEnum::eNone;
}

Runner::~Runner()
{
    std::ignore = Stop();
}

RunStatus Runner::StartInstance(const String& instanceID, const String& runtimeDir, const RunParameters& params)
{
    (void)runtimeDir;

    RunStatus status = {};

    status.mInstanceID = instanceID;
    status.mState      = InstanceRunStateEnum::eFailed;

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

    LOG_DBG() << "Start service instance: instanceID=" << instanceID << ", startInterval=" << fixedParams.mStartInterval
              << ", startBurst=" << fixedParams.mStartBurst << ", restartInterval=" << fixedParams.mRestartInterval;

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

    LOG_DBG() << "Start instance: name=" << unitName.c_str() << ", unitStatus=" << status.mState
              << ", instanceID=" << instanceID << ", err=" << status.mError;

    return status;
}

Error Runner::StopInstance(const String& instanceID)
{
    LOG_DBG() << "Stop service instance: " << instanceID;

    const auto unitName = CreateSystemdUnitName(instanceID);

    {
        std::lock_guard lock {mMutex};

        mRunningUnits.erase(unitName);
    }

    auto err = mSystemd->StopUnit(unitName, "replace", cDefaultStopTimeout);
    if (!err.IsNone()) {
        if (err.Is(ErrorEnum::eNotFound)) {
            LOG_DBG() << "Service not loaded: id=" << instanceID;

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

std::shared_ptr<SystemdConnItf> Runner::CreateSystemdConn()
{
    return std::make_shared<SystemdConn>();
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
            LOG_ERR() << "Systemd list units failed, err=" << err;

            return;
        }

        bool unitChanged = false;

        for (const auto& unit : units) {
            // Update starting units
            auto startUnitIt = mStartingUnits.find(unit.mName);
            if (startUnitIt != mStartingUnits.end()) {
                startUnitIt->second.mRunState = unit.mActiveState;
                startUnitIt->second.mExitCode = unit.mExitCode;

                // systemd doesn't change the state of failed unit => notify listener about final state.
                if (unit.mActiveState == UnitStateEnum::eFailed) {
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

Array<RunStatus> Runner::GetRunningInstances() const
{
    mRunningInstances.clear();

    std::transform(
        mRunningUnits.begin(), mRunningUnits.end(), std::back_inserter(mRunningInstances), [](const auto& unit) {
            const auto instanceID = CreateInstanceID(unit.first);

            auto error = unit.second.mExitCode.HasValue() ? Error(unit.second.mExitCode.GetValue()) : Error();

            return RunStatus {instanceID.c_str(), unit.second.mRunState, error};
        });

    return Array(mRunningInstances.data(), mRunningInstances.size());
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

RetWithError<InstanceRunState> Runner::GetStartingUnitState(const std::string& unitName, Duration startInterval)
{
    const auto timeout = std::chrono::milliseconds(startInterval.Milliseconds());

    auto [initialStatus, err] = mSystemd->GetUnitStatus(unitName);
    if (!err.IsNone()) {
        return {InstanceRunStateEnum::eFailed, AOS_ERROR_WRAP(Error(err, "failed to get unit status"))};
    }

    {
        std::unique_lock lock {mMutex};

        mStartingUnits[unitName].mRunState = initialStatus.mActiveState;
        mStartingUnits[unitName].mExitCode = initialStatus.mExitCode;

        // Wait specified duration for unit state updates.
        std::ignore   = mStartingUnits[unitName].mCondVar.wait_for(lock, timeout);
        auto runState = mStartingUnits[unitName].mRunState;
        auto exitCode = mStartingUnits[unitName].mExitCode;

        mStartingUnits.erase(unitName);

        if (runState.GetValue() != UnitStateEnum::eActive) {

            const auto errMsg = "failed to start unit";
            err = exitCode.HasValue() ? Error(exitCode.GetValue(), errMsg) : Error(ErrorEnum::eFailed, errMsg);

            return {InstanceRunStateEnum::eFailed, AOS_ERROR_WRAP(err)};
        }

        mRunningUnits[unitName] = RunningUnitData {InstanceRunStateEnum::eActive, exitCode};

        return {InstanceRunStateEnum::eActive, ErrorEnum::eNone};
    }
}

std::string Runner::CreateSystemdUnitName(const String& instance)
{
    return Poco::format(cSystemdUnitNameTemplate, std::string(instance.CStr()));
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

} // namespace aos::sm::runner
