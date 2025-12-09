/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <algorithm>
#include <chrono>
#include <systemd/sd-bus-protocol.h>
#include <thread>

#include <core/common/tools/memory.hpp>

#include <common/utils/exception.hpp>
#include <sm/logger/logmodule.hpp>

#include "systemdconn.hpp"

namespace aos::sm::utils {

/***********************************************************************************************************************
 * Statics
 **********************************************************************************************************************/

namespace {

RetWithError<UnitState> ConvertToUnitState(const std::string& src)
{
    UnitState result;

    auto err = result.FromString(src.c_str());

    return {result, err};
}

} // namespace

/***********************************************************************************************************************
 * Implementation
 **********************************************************************************************************************/

SystemdConn::SystemdConn()
{
    // According to https://www.freedesktop.org/software/systemd/man/255/sd_bus_open.html
    // sd_bus_open_system creates connection object associated with the calling thread,
    // Consequently it might be unsafe to use it from multiple threads even with proper synchronization.
    // Still leave the current approach as no problems observed yet.
    auto rv = sd_bus_open_system(&mBus);
    if (rv < 0) {
        AOS_ERROR_THROW(Error(-rv), "can't open systemd");
    }
}

SystemdConn::~SystemdConn()
{
    sd_bus_unref(mBus);
}

RetWithError<std::vector<UnitStatus>> SystemdConn::ListUnits()
{
    std::lock_guard lock {mMutex};

    sd_bus_error          error   = SD_BUS_ERROR_NULL;
    sd_bus_message*       reply   = nullptr;
    [[maybe_unused]] auto freeErr = DeferRelease(&error, sd_bus_error_free);

    auto rv = sd_bus_call_method(mBus, cDestination, cPath, cInterface, "ListUnits", &error, &reply, nullptr);
    if (rv < 0) {
        return {{}, AOS_ERROR_WRAP(-rv)};
    }

    [[maybe_unused]] auto freeMsg = DeferRelease(reply, sd_bus_message_unref);

    rv = sd_bus_message_enter_container(reply, SD_BUS_TYPE_ARRAY, "(ssssssouso)");
    if (rv < 0) {
        return {{}, AOS_ERROR_WRAP(-rv)};
    }

    std::vector<UnitStatus> units;

    while ((rv = sd_bus_message_enter_container(reply, SD_BUS_TYPE_STRUCT, "ssssssouso")) > 0) {
        const char* name        = nullptr;
        const char* activeState = nullptr;
        const char* objectPath  = nullptr;
        const char* ignore      = nullptr;

        rv = sd_bus_message_read(
            reply, "sssssso", &name, &ignore, &ignore, &activeState, &ignore, &ignore, &objectPath);
        if (rv < 0) {
            return {{}, AOS_ERROR_WRAP(-rv)};
        }

        Error      err;
        UnitStatus status;

        status.mName = name;

        Tie(status.mActiveState, err) = ConvertToUnitState(activeState);
        if (!err.IsNone()) {
            return {{}, AOS_ERROR_WRAP(err)};
        }

        if (status.mActiveState != UnitStateEnum::eActive) {
            status.mExitCode = GetExitCode(objectPath);
        }

        units.push_back(status);

        rv = sd_bus_message_skip(reply, "uso");
        if (rv < 0) {
            return {{}, AOS_ERROR_WRAP(-rv)};
        }

        rv = sd_bus_message_exit_container(reply);
        if (rv < 0) {
            return {{}, AOS_ERROR_WRAP(-rv)};
        }
    }

    rv = sd_bus_message_exit_container(reply);
    if (rv < 0) {
        return {{}, AOS_ERROR_WRAP(-rv)};
    }

    return {units, ErrorEnum::eNone};
}

RetWithError<UnitStatus> SystemdConn::GetUnitStatus(const std::string& name)
{
    std::lock_guard lock {mMutex};

    sd_bus_message* reply = nullptr;

    auto rv = sd_bus_call_method(mBus, cDestination, cPath, cInterface, "GetUnit", nullptr, &reply, "s", name.c_str());
    if (rv < 0) {
        return {{}, AOS_ERROR_WRAP(-rv)};
    }

    [[maybe_unused]] auto freeMsg = DeferRelease(reply, sd_bus_message_unref);

    const char* unitPath = nullptr;

    rv = sd_bus_message_read(reply, "o", &unitPath);
    if (rv < 0) {
        return {{}, AOS_ERROR_WRAP(-rv)};
    }

    // Get active state
    sd_bus_error          stateError   = SD_BUS_ERROR_NULL;
    sd_bus_message*       stateReply   = nullptr;
    [[maybe_unused]] auto freeStateErr = DeferRelease(&stateError, sd_bus_error_free);

    rv = sd_bus_get_property(
        mBus, cDestination, unitPath, "org.freedesktop.systemd1.Unit", "ActiveState", &stateError, &stateReply, "s");

    if (rv < 0) {
        return {{}, AOS_ERROR_WRAP(-rv)};
    }

    [[maybe_unused]] auto freeStateMsg = DeferRelease(stateReply, sd_bus_message_unref);

    const char* activeState = nullptr;
    rv                      = sd_bus_message_read(stateReply, "s", &activeState);
    if (rv < 0) {
        return {{}, AOS_ERROR_WRAP(-rv)};
    }

    Error      err;
    UnitStatus status;

    status.mName = name;

    Tie(status.mActiveState, err) = ConvertToUnitState(activeState);
    if (!err.IsNone()) {
        return {{}, AOS_ERROR_WRAP(err)};
    }

    if (status.mActiveState != UnitStateEnum::eActive) {
        status.mExitCode = GetExitCode(unitPath);
    }

    return {status, ErrorEnum::eNone};
}

Error SystemdConn::StartUnit(const std::string& name, const std::string& mode, const Duration& timeout)
{
    std::lock_guard lock {mMutex};

    sd_bus_slot* slot = nullptr;

    auto rv = sd_bus_match_signal(mBus, &slot, nullptr, cPath, cInterface, "JobRemoved", nullptr, nullptr);
    if (rv < 0) {
        return AOS_ERROR_WRAP(-rv);
    }

    [[maybe_unused]] auto freeSlot = DeferRelease(slot, &sd_bus_slot_unref);
    sd_bus_message*       msg      = nullptr;

    rv = sd_bus_call_method(
        mBus, cDestination, cPath, cInterface, "StartUnit", nullptr, &msg, "ss", name.c_str(), mode.c_str());
    if (rv < 0) {
        return AOS_ERROR_WRAP(-rv);
    }

    [[maybe_unused]] auto freeMsg = DeferRelease(msg, sd_bus_message_unref);
    const char*           jobPath = nullptr;

    rv = sd_bus_message_read(msg, "o", &jobPath);
    if (rv < 0) {
        return AOS_ERROR_WRAP(-rv);
    }

    return WaitForJobCompletion(jobPath, timeout);
}

Error SystemdConn::StopUnit(const std::string& name, const std::string& mode, const Duration& timeout)
{
    std::lock_guard lock {mMutex};

    sd_bus_slot* slot = nullptr;

    auto rv = sd_bus_match_signal(mBus, &slot, nullptr, cPath, cInterface, "JobRemoved", nullptr, nullptr);
    if (rv < 0) {
        return AOS_ERROR_WRAP(-rv);
    }

    [[maybe_unused]] auto freeSlot = DeferRelease(slot, &sd_bus_slot_unref);

    sd_bus_message*       msg     = nullptr;
    sd_bus_error          error   = SD_BUS_ERROR_NULL;
    [[maybe_unused]] auto freeErr = DeferRelease(&error, sd_bus_error_free);

    rv = sd_bus_call_method(
        mBus, cDestination, cPath, cInterface, "StopUnit", &error, &msg, "ss", name.c_str(), mode.c_str());
    if (rv < 0) {
        if (sd_bus_error_has_name(&error, cNoSuchUnitErr)) {
            return ErrorEnum::eNotFound;
        }
        return AOS_ERROR_WRAP(-rv);
    }

    [[maybe_unused]] auto freeMsg = DeferRelease(msg, sd_bus_message_unref);
    const char*           jobPath = nullptr;

    rv = sd_bus_message_read(msg, "o", &jobPath);
    if (rv < 0) {
        return AOS_ERROR_WRAP(-rv);
    }

    return WaitForJobCompletion(jobPath, timeout);
}

Error SystemdConn::ResetFailedUnit(const std::string& name)
{
    std::lock_guard lock {mMutex};

    sd_bus_error          error   = SD_BUS_ERROR_NULL;
    sd_bus_message*       reply   = nullptr;
    [[maybe_unused]] auto freeErr = DeferRelease(&error, sd_bus_error_free);

    auto rv = sd_bus_call_method(
        mBus, cDestination, cPath, cInterface, "ResetFailedUnit", &error, &reply, "s", name.c_str());
    if (rv < 0) {
        if (sd_bus_error_has_name(&error, cNoSuchUnitErr)) {
            return ErrorEnum::eNotFound;
        }

        return AOS_ERROR_WRAP(-rv);
    }

    [[maybe_unused]] auto freeMsg = DeferRelease(reply, sd_bus_message_unref);

    return ErrorEnum::eNone;
}

Error SystemdConn::WaitForJobCompletion(const char* jobPath, const aos::Duration& timeout)
{
    const aos::Time startTime = aos::Time::Now();
    const aos::Time endTime   = startTime.Add(timeout);

    while (true) {
        sd_bus_message* msg = nullptr;

        const aos::Time now = aos::Time::Now();
        if (endTime < now) {
            return AOS_ERROR_WRAP(ErrorEnum::eTimeout);
        }

        auto rv = sd_bus_process(mBus, &msg);
        if (rv == 0) {
            rv = sd_bus_wait(mBus, endTime.Sub(now).Microseconds());
            if (rv < 0) {
                return AOS_ERROR_WRAP(-rv);
            } else if (rv == 0) {
                return AOS_ERROR_WRAP(ErrorEnum::eTimeout);
            }
        } else if (rv < 0) {
            return AOS_ERROR_WRAP(-rv);
        } else {
            auto freeMsg = DeferRelease(msg, &sd_bus_message_unref);

            auto [completed, err] = HandleJobRemove(msg, jobPath);
            if (completed) {
                return err;
            }
        }
    }

    return ErrorEnum::eFailed;
}

std::pair<bool, Error> SystemdConn::HandleJobRemove(sd_bus_message* msg, const char* jobPath)
{
    const char* member    = sd_bus_message_get_member(msg);
    const char* interface = sd_bus_message_get_interface(msg);

    if (!member || !interface || String(member) != "JobRemoved" || std::string(interface) != cInterface) {
        return {false, ErrorEnum::eFailed};
    }

    uint32_t    jobId      = 0;
    const char* jobCurPath = nullptr;
    const char* unitName   = nullptr;
    const char* result     = nullptr;

    int rv = sd_bus_message_read(msg, "uoss", &jobId, &jobCurPath, &unitName, &result);
    if (rv < 0) {
        return {true, AOS_ERROR_WRAP(rv)};
    }

    if (String(jobCurPath) == jobPath) {
        if (String(result) == "done") {
            return {true, ErrorEnum::eNone};
        } else {
            auto errMsg = std::string("job finished with status=") + result;

            return {true, AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, errMsg.c_str()))};
        }
    }

    return {false, ErrorEnum::eNone};
}

Optional<int32_t> SystemdConn::GetExitCode(const char* serviceName)
{
    sd_bus_message* serviceExitReply = nullptr;

    auto rv = sd_bus_get_property(mBus, cDestination, serviceName, "org.freedesktop.systemd1.Service", "ExecMainStatus",
        nullptr, &serviceExitReply, "i");
    if (rv < 0) {
        return {};
    } else {
        [[maybe_unused]] auto freeExitReply = DeferRelease(serviceExitReply, sd_bus_message_unref);
        int32_t               exitCode      = 0;

        rv = sd_bus_message_read(serviceExitReply, "i", &exitCode);
        if (rv < 0) {
            return {};
        }

        return exitCode;
    }
}

} // namespace aos::sm::utils
