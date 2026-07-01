/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <core/common/tools/logger.hpp>

#include <common/utils/utils.hpp>

#include "crunrunner.hpp"
#include "libcrun.hpp"

namespace aos::sm::launcher {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error CRunRunner::Init(const std::string& runtimeDir)
{
    LOG_DBG() << "Initialize crun runner" << Log::Field("runtimeDir", runtimeDir.c_str());

    mRuntimeDir = runtimeDir;

    return ErrorEnum::eNone;
}

Error CRunRunner::StartContainer(const std::string& instanceID)
{
    LOG_DBG() << "Start crun container" << Log::Field("instanceID", instanceID.c_str());

    const std::string bundleDir  = mRuntimeDir + "/" + instanceID;
    const std::string configPath = bundleDir + "/config.json";
    const std::string pidFile    = mRuntimeDir + "/" + instanceID + "/.pid";
    /*
        libcrun_error_t   err = nullptr;
        libcrun_context_t ctx = MakeContext(cStateRoot, instanceID);

        ctx.bundle   = bundleDir.c_str();
        ctx.pid_file = pidFile.c_str();
        ctx.detach   = true;

        // Pre-delete any leftover container state (ignore failure).
        libcrun_container_kill(&ctx, instanceID.c_str(), "SIGKILL", &err);
        libcrun_error_release(&err);

        auto container = DeferRelease(libcrun_container_load_from_file(configPath.c_str(), &err),
       libcrun_container_free); if (!container) { return AOS_ERROR_WRAP(ReleaseLibcrunError(err));
        }

        if (libcrun_container_run(&ctx, container.Get(), 0, &err) < 0) {
            return AOS_ERROR_WRAP(ReleaseLibcrunError(err));
        }
*/

    // The container process started by "run -d" keeps running long after crun itself exits, so it must
    // inherit real stdout/stderr (rather than ExecCommand's pipe, which gets closed once crun exits) for its
    // own output to keep working.
    if (auto err = common::utils::ExecDetachedCommand(
            {cCRunExecutable, "--root", cStateRoot, "run", "-d", "-b", bundleDir, instanceID});
        !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    {
        std::lock_guard lock {mMutex};

        mManagedInstances.insert(instanceID);
    }

    LOG_DBG() << "Started crun container" << Log::Field("instanceID", instanceID.c_str());

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
    LOG_INF() << "Stop crun container" << Log::Field("instanceID", instanceID.c_str());

    auto [_, err] = common::utils::ExecCommand({cCRunExecutable, "--root", cStateRoot, "kill", instanceID, "SIGKILL"});
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }
    /*
        libcrun_error_t   err = nullptr;
        libcrun_context_t ctx = MakeContext(cStateRoot, instanceID);

        ctx.pid_file = pidFile.c_str();

        if (libcrun_container_kill(&ctx, instanceID.c_str(), "SIGKILL", &err) < 0) {
            return AOS_ERROR_WRAP(ReleaseLibcrunError(err));
        }
    */
    return ErrorEnum::eNone;
}

Error CRunRunner::RemoveContainer(const std::string& instanceID)
{
    LOG_INF() << "Remove crun container" << Log::Field("instanceID", instanceID.c_str());

    {
        std::lock_guard lock {mMutex};

        mManagedInstances.erase(instanceID);
    }

    auto [_, err] = common::utils::ExecCommand({cCRunExecutable, "--root", cStateRoot, "delete", "-f", instanceID});
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    /*
        libcrun_error_t   err = nullptr;
        libcrun_context_t ctx = MakeContext(cStateRoot, instanceID);

        if (libcrun_container_delete(&ctx, nullptr, instanceID.c_str(), true, &err) < 0) {
            return AOS_ERROR_WRAP(ReleaseLibcrunError(err));
        }
    */
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

    if (libcrun_read_container_status(&crunStatus, cStateRoot, instanceID.c_str(), &err) < 0) {
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
