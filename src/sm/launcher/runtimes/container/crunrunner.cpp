/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sys/wait.h>

#include <filesystem>
#include <fstream>

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
        return Error((err->status == ENOENT || err->status == ESRCH) ? ErrorEnum::eNotFound : ErrorEnum::eFailed, msg);
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

    // Not run with "-d": crun stays alive in the foreground as the container's subreaper and exits with the
    // container's own exit code once it terminates, which is how ReapExitedContainers() captures it below.
    // It still inherits this process's real stdout/stderr (no pipe), same as the previous detached mode.
    auto [pid, err]
        = common::utils::ExecAsyncCommand({mCRunExecutable, "--root", mStateRoot, "run", "-b", bundleDir, instanceID});
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    {
        std::lock_guard lock {mMutex};

        mPids[instanceID] = pid;
        mExitCodes.erase(instanceID);
    }

    return ErrorEnum::eNone;
}

RetWithError<ContainerStatus> CRunRunner::GetContainerStatus(const std::string& instanceID)
{
    LOG_DBG() << "Get crun container status" << Log::Field("instanceID", instanceID.c_str());

    ReapExitedContainers();

    return CheckProcessAlive(instanceID);
}

RetWithError<std::vector<ContainerStatus>> CRunRunner::ListContainers()
{
    ReapExitedContainers();

    // Enumerate crun's own state root instead of keeping a separate in-memory set of managed instances: this
    // stays correct even for containers we did not start ourselves (WatchInstance re-attaching after a restart)
    // and can never drift from what crun itself considers alive. Relies on mStateRoot being exclusively ours -
    // libcrun_get_containers_list() fails outright if the directory holds anything crun didn't create.
    libcrun_error_t           err  = nullptr;
    libcrun_container_list_t* list = nullptr;

    if (libcrun_get_containers_list(&list, mStateRoot.c_str(), &err) < 0) {
        return {{}, AOS_ERROR_WRAP(ReleaseLibcrunError(err))};
    }

    auto release = DeferRelease(list, libcrun_free_containers_list);

    std::vector<ContainerStatus> result;

    for (auto* entry = list; entry != nullptr; entry = entry->next) {
        auto [status, statusErr] = CheckProcessAlive(entry->name);
        if (!statusErr.IsNone()) {
            LOG_WRN() << "Failed to check process status" << Log::Field("instanceID", entry->name)
                      << Log::Field(statusErr);
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

        mPids.erase(instanceID);
        mExitCodes.erase(instanceID);
    }

    // Clears any exit code left over from a previous run of this instance ID, so a restart (RemoveContainer
    // followed by StartContainer, see Runner::RestartInstances()) can't resurface a stale value.
    std::error_code fsErr;
    std::filesystem::remove(ExitCodeFilePath(instanceID), fsErr);

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

void CRunRunner::ReapExitedContainers()
{
    std::lock_guard lock {mMutex};

    for (auto it = mPids.begin(); it != mPids.end();) {
        int status = 0;

        if (waitpid(it->second, &status, WNOHANG) <= 0) {
            ++it;
            continue;
        }

        const int32_t exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

        mExitCodes[it->first] = exitCode;
        PersistExitCode(it->first, exitCode);

        it = mPids.erase(it);
    }
}

std::string CRunRunner::ExitCodeFilePath(const std::string& instanceID) const
{
    return mRuntimeDir + "/" + instanceID + "/exitcode";
}

void CRunRunner::PersistExitCode(const std::string& instanceID, int32_t exitCode) const
{
    std::ofstream file(ExitCodeFilePath(instanceID), std::ios::trunc);
    if (!file) {
        LOG_WRN() << "Failed to persist container exit code" << Log::Field("instanceID", instanceID.c_str());

        return;
    }

    file << exitCode;
}

std::optional<int32_t> CRunRunner::ReadPersistedExitCode(const std::string& instanceID) const
{
    std::ifstream file(ExitCodeFilePath(instanceID));

    int32_t exitCode = 0;

    if (!file || !(file >> exitCode)) {
        return std::nullopt;
    }

    return exitCode;
}

RetWithError<ContainerStatus> CRunRunner::CheckProcessAlive(const std::string& instanceID) const
{
    ContainerStatus status;

    status.mInstanceID = instanceID;

    {
        std::lock_guard lock {mMutex};

        if (auto it = mExitCodes.find(instanceID); it != mExitCodes.end()) {
            status.mState    = InstanceStateEnum::eFailed;
            status.mExitCode = it->second;

            return {status, ErrorEnum::eNone};
        }

        // We hold a live, unreaped pid for this instance: ReapExitedContainers() (called just before this, in
        // GetContainerStatus()/ListContainers()) already confirmed via waitpid(WNOHANG) that it hasn't exited.
        // That is more current than crun's on-disk state, which may not exist yet immediately after
        // StartContainer() returns - it only waits for fork(), not for crun to finish its own OCI setup and
        // write state.json, so without this check a status query racing a fresh start would misreport eFailed.
        if (mPids.find(instanceID) != mPids.end()) {
            status.mState = InstanceStateEnum::eActive;

            return {status, ErrorEnum::eNone};
        }
    }

    // Fallback for containers we didn't start ourselves in this process (e.g. WatchInstance re-attaching after
    // an SM restart): no tracked pid to reap, but ReapExitedContainers() may have persisted the exit code to
    // disk before this process last exited, so check that before falling back to a bare liveness check.
    if (auto exitCode = ReadPersistedExitCode(instanceID); exitCode.has_value()) {
        status.mState    = InstanceStateEnum::eFailed;
        status.mExitCode = *exitCode;

        return {status, ErrorEnum::eNone};
    }

    status.mState = InstanceStateEnum::eActive;

    libcrun_error_t            err        = nullptr;
    libcrun_container_status_t crunStatus = {};

    if (libcrun_read_container_status(&crunStatus, mStateRoot.c_str(), instanceID.c_str(), &err) < 0) {
        libcrun_error_release(&err);
        status.mState = InstanceStateEnum::eFailed;

        return {status, ErrorEnum::eNone};
    }

    const int running = libcrun_is_container_running(&crunStatus, &err);

    libcrun_free_container_status(&crunStatus);

    if (running <= 0) {
        libcrun_error_release(&err);

        status.mState = InstanceStateEnum::eFailed;
    }

    return {status, ErrorEnum::eNone};
}

} // namespace aos::sm::launcher
