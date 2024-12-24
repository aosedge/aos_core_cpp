/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <Poco/Format.h>
#include <algorithm>
#include <filesystem>

#include <aos/common/tools/fs.hpp>
#include <logger/logmodule.hpp>
#include <utils/exception.hpp>

#include "runner.hpp"

namespace aos::sm::runner {

/***********************************************************************************************************************
 * Statics
 **********************************************************************************************************************/

namespace {

inline unsigned ToSec(Duration duration)
{
    return duration / Time::cSeconds;
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

    try {
        mSystemd = CreateSystemdConn();
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, e.what()));
    }

    return ErrorEnum::eNone;
}

Error Runner::Start()
{
    LOG_DBG() << "Start runner";

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

        mSystemd.reset();
    }

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

    LOG_DBG() << "Start service instance: instanceID=" << instanceID
              << ", StartInterval=" << ToSec(params.mStartInterval) << ", StartBurst=" << ToSec(params.mStartBurst)
              << ", RestartInterval=" << ToSec(params.mRestartInterval);

    // Fix run parameters.
    RunParameters fixedParams = params;

    if (params.mStartInterval == 0) {
        fixedParams.mStartInterval = cDefaultStartInterval;
    }

    if (params.mStartBurst == 0) {
        fixedParams.mStartBurst = cDefaultStartBurst;
    }

    if (params.mRestartInterval == 0) {
        fixedParams.mRestartInterval = cDefaultRestartInterval;
    }

    // Create systemd service file.
    if (status.mError = SetRunParameters(instanceID, fixedParams); !status.mError.IsNone()) {
        return status;
    }

    // Start unit.
    const auto unitName  = CreateSystemdUnitName(instanceID);
    const auto startTime = static_cast<Duration>(cStartTimeMultiplier * fixedParams.mStartInterval);

    if (status.mError = mSystemd->StartUnit(unitName, "replace", startTime); !status.mError.IsNone()) {
        return status;
    }

    // Get unit status.
    UnitStatus unitStatus;

    Tie(unitStatus, status.mError) = mSystemd->GetUnitStatus(unitName);

    status.mState = unitStatus.mActiveState;

    {
        std::lock_guard lock {mMutex};

        mRunningUnits[unitName] = status.mState;
    }

    LOG_DBG() << "Start instance: name=" << unitName.c_str() << ", unitStatus=" << unitStatus.mActiveState
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
        } else {
            return err;
        }
    }

    if (err = mSystemd->ResetFailedUnit(unitName); !err.IsNone()) {
        if (!err.Is(ErrorEnum::eNotFound)) {
            return err;
        }
    }

    return RemoveRunParameters(unitName.c_str());
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
            auto runUnitIt = mRunningUnits.find(unit.mName);
            if (runUnitIt == mRunningUnits.end()) {
                continue;
            }

            auto& unitStatus = runUnitIt->second;

            if (unitStatus != unit.mActiveState) {
                unitStatus  = unit.mActiveState;
                unitChanged = true;
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
            return RunStatus {unit.first.c_str(), unit.second, Error()};
        });

    return Array(mRunningInstances.data(), mRunningInstances.size());
}

Error Runner::SetRunParameters(const String& unitName, const RunParameters& params)
{
    const std::string parametersFormat = "[Unit]\n"
                                         "StartLimitIntervalSec=%us\n"
                                         "StartLimitBurst=%ld\n\n"
                                         "[Service]\n"
                                         "RestartSec=%us\n";

    if (params.mStartInterval < aos::Time::cMicroseconds * 1
        || params.mRestartInterval < aos::Time::cMicroseconds * 1) {
        return AOS_ERROR_WRAP(ErrorEnum::eInvalidArgument);
    }

    std::string formattedContent = Poco::format(
        parametersFormat, ToSec(params.mStartInterval), params.mStartBurst, ToSec(params.mRestartInterval));

    const std::string parametersDir = GetSystemdDropInsDir() + "/" + unitName.CStr() + ".d";

    if (auto err = CreateDir(parametersDir, 0755U); !err.IsNone()) {
        return err;
    }

    const auto paramsFile = parametersDir + "/" + cParametersFileName;

    return FS::WriteStringToFile(paramsFile.c_str(), formattedContent.c_str(), 0644U);
}

Error Runner::RemoveRunParameters(const String& unitName)
{
    const std::string parametersDir = GetSystemdDropInsDir() + "/" + unitName.CStr() + ".d";

    return FS::RemoveAll(parametersDir.c_str());
}

std::string Runner::CreateSystemdUnitName(const String& instance)
{
    return Poco::format(cSystemdUnitNameTemplate, std::string(instance.CStr()));
}

} // namespace aos::sm::runner
