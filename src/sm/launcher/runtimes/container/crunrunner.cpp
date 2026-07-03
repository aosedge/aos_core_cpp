/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <core/common/tools/logger.hpp>

#include <common/utils/exec.hpp>

#include "crunrunner.hpp"
#include "libcrun.hpp"

namespace aos::sm::launcher {

namespace {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

libcrun_context_t MakeContext(std::string_view stateRoot, const std::string& id)
{
    libcrun_context_t ctx = {};

    ctx.state_root        = stateRoot.data();
    ctx.id                = id.c_str();
    ctx.fifo_exec_wait_fd = -1;

    return ctx;
}

Error ReleaseLibcrunError(libcrun_error_t& err)
{
    auto release = DeferRelease(&err, libcrun_error_release);

    const char* msg = (err && err->msg) ? err->msg : "unknown error";

    if (err) {
        return Error(err->status == ENOENT ? ErrorEnum::eNotFound : ErrorEnum::eFailed, msg);
    }

    return Error(ErrorEnum::eFailed, msg);
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error CRunRunner::Init(const std::string& runtimeDir, const std::string& stateRoot, const std::string& crunExecutable)
{
    LOG_DBG() << "Initialize crun runner" << Log::Field("runtimeDir", runtimeDir.c_str())
              << Log::Field("stateRoot", stateRoot.c_str()) << Log::Field("crunExecutable", crunExecutable.c_str());

    mRuntimeDir     = runtimeDir;
    mStateRoot      = stateRoot;
    mCRunExecutable = crunExecutable;

    return ErrorEnum::eNone;
}

Error CRunRunner::StartContainer(const std::string& instanceID)
{
    LOG_DBG() << "Start crun container" << Log::Field("instanceID", instanceID.c_str());

    const std::string bundleDir = mRuntimeDir + "/" + instanceID;

    // The container process started by "run -d" keeps running long after crun itself exits, so it must
    // inherit real stdout/stderr (rather than ExecCommand's pipe, which gets closed once crun exits) for its
    // own output to keep working.
    if (auto err = common::utils::ExecDetachedCommand(
            {mCRunExecutable, "--root", mStateRoot, "run", "-d", "-b", bundleDir, instanceID});
        !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    {
        std::lock_guard lock {mMutex};

        mManagedInstances.insert(instanceID);
    }

    return ErrorEnum::eNone;
}

RetWithError<ContainerStatus> CRunRunner::GetContainerStatus(const std::string& instanceID)
{
    LOG_DBG() << "Get crun container status" << Log::Field("instanceID", instanceID.c_str());

    return CheckProcessAlive(instanceID);
}

RetWithError<std::vector<ContainerStatus>> CRunRunner::ListContainers()
{
    std::set<std::string> instances;

    {
        std::lock_guard lock {mMutex};

        instances = mManagedInstances;
    }

    std::vector<ContainerStatus> result;

    for (const auto& id : instances) {
        auto [status, err] = CheckProcessAlive(id);
        if (!err.IsNone()) {
            LOG_WRN() << "Failed to check process status" << Log::Field("instanceID", id.c_str()) << Log::Field(err);
        }

        result.push_back(status);
    }

    return {result, ErrorEnum::eNone};
}

Error CRunRunner::StopContainer(const std::string& instanceID)
{
    LOG_DBG() << "Stop crun container" << Log::Field("instanceID", instanceID.c_str());

    libcrun_error_t   err = nullptr;
    libcrun_context_t ctx = MakeContext(mStateRoot, instanceID);

    if (libcrun_container_kill(&ctx, instanceID.c_str(), "SIGKILL", &err) < 0) {
        return AOS_ERROR_WRAP(ReleaseLibcrunError(err));
    }

    return ErrorEnum::eNone;
}

Error CRunRunner::RemoveContainer(const std::string& instanceID)
{
    LOG_DBG() << "Remove crun container" << Log::Field("instanceID", instanceID.c_str());

    {
        std::lock_guard lock {mMutex};

        mManagedInstances.erase(instanceID);
    }

    libcrun_error_t   err = nullptr;
    libcrun_context_t ctx = MakeContext(mStateRoot, instanceID);

    if (libcrun_container_delete(&ctx, nullptr, instanceID.c_str(), true, &err) < 0) {
        return AOS_ERROR_WRAP(ReleaseLibcrunError(err));
    }

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

RetWithError<ContainerStatus> CRunRunner::CheckProcessAlive(const std::string& instanceID) const
{
    ContainerStatus status;

    status.mInstanceID = instanceID;

    libcrun_error_t            err        = nullptr;
    libcrun_container_status_t crunStatus = {};

    if (libcrun_read_container_status(&crunStatus, mStateRoot.c_str(), instanceID.c_str(), &err) < 0) {
        libcrun_error_release(&err);
        status.mState = InstanceStateEnum::eInactive;

        return {status, ErrorEnum::eNone};
    }

    const int running = libcrun_is_container_running(&crunStatus, &err);

    libcrun_free_container_status(&crunStatus);

    if (running < 0) {
        libcrun_error_release(&err);
        status.mState = InstanceStateEnum::eFailed;

        return {status, ErrorEnum::eNone};
    }

    status.mState = (running > 0) ? InstanceStateEnum::eActive : InstanceStateEnum::eInactive;

    return {status, ErrorEnum::eNone};
}

} // namespace aos::sm::launcher
