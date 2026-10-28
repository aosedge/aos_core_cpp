/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string_view>
#include <system_error>
#include <thread>

#include <Poco/JSON/Parser.h>

#include <core/common/tools/logger.hpp>
#include <core/common/tools/memory.hpp>

#include <common/utils/exception.hpp>
#include <common/utils/filesystem.hpp>

#include "crunrunner.hpp"
#include "libcrun.hpp"

// __GLIBC_PREREQ is only defined by glibc, so it can't be combined with defined(__GLIBC__) in a single #if.
#if defined(__GLIBC__) && defined(__GLIBC_PREREQ)
#if __GLIBC_PREREQ(2, 34)
#define HAVE_SPAWN_ADDCLOSEFROM
#endif
#endif

extern char** environ;

namespace aos::sm::launcher {

namespace {

/***********************************************************************************************************************
 * Consts
 **********************************************************************************************************************/

constexpr auto cConmonPIDFile        = "conmon.pid";
constexpr auto cContainerPIDFile     = "container.pid";
constexpr auto cExitFile             = "exit";
constexpr auto cOOMFile              = "oom";
constexpr auto cSyncPipeEnv          = "_OCI_SYNCPIPE";
constexpr auto cSyncPipeFD           = 3;
constexpr auto cCreateTimeout        = std::chrono::seconds(30);
constexpr auto cConmonExitTimeout    = std::chrono::seconds(5);
constexpr auto cConmonExitPollPeriod = std::chrono::milliseconds(50);

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

void CloseFD(const int* fd) // NOSONAR cpp:M23_058 - POSIX file descriptor type
{
    if (*fd >= 0) {
        static_cast<void>(close(*fd));
    }
}

// Makes the spawned process close every inheritable fd from minFD up.
//
// posix_spawn_file_actions_addclosefrom_np() is a glibc 2.34+ extension. Without it, the fds that are currently open
// without FD_CLOEXEC are closed one by one instead: this can't see an fd another thread opens in between, but it's safe
// for an fd another thread closes in between, since glibc ignores close actions on fds that are no longer open.
Error AddCloseFrom(posix_spawn_file_actions_t& actions, int minFD) // NOSONAR cpp:M23_058 - POSIX fd type
{
#ifdef HAVE_SPAWN_ADDCLOSEFROM
    if (auto rc = posix_spawn_file_actions_addclosefrom_np(&actions, minFD); rc != 0) {
        return Error(rc, "can't close inherited fds");
    }
#else
    std::error_code ec;

    for (std::filesystem::directory_iterator it("/proc/self/fd", ec), end; !ec && it != end; it.increment(ec)) {
        const auto name = it->path().filename().string();
        int        fd   = -1; // NOSONAR cpp:M23_058 - POSIX fd type

        if (std::from_chars(name.data(), name.data() + name.size(), fd).ec != std::errc() || fd < minFD) {
            continue;
        }

        if (const auto flags = fcntl(fd, F_GETFD); flags < 0 || (flags & FD_CLOEXEC) != 0) {
            continue;
        }

        if (auto rc = posix_spawn_file_actions_addclose(&actions, fd); rc != 0) {
            return Error(rc, "can't close inherited fds");
        }
    }

    if (ec) {
        return Error(ErrorEnum::eFailed, ("can't list open fds: " + ec.message()).c_str());
    }
#endif

    return ErrorEnum::eNone;
}

// Spawns conmon with the write end of its sync pipe placed at cSyncPipeFD and waits for conmon's launcher process to
// exit: conmon double-forks, so that returns as soon as the monitor process is detached, not when the container is
// created. The container creation result is then reported by the monitor through the sync pipe whose read end is
// returned in syncFD.
Error SpawnConmon(const std::vector<std::string>& args, int& syncFD) // NOSONAR cpp:M23_058 - POSIX file descriptor type
{
    std::array<int, 2> pipeFDs {}; // NOSONAR cpp:M23_058 - pipe2() fd type

    if (pipe2(pipeFDs.data(), O_CLOEXEC) != 0) {
        return Error(errno, "can't create conmon sync pipe");
    }

    auto closeRead = DeferRelease(&pipeFDs[0], CloseFD);

    // dup2() onto the very same fd number leaves FD_CLOEXEC set, which would close the pipe on exec. Move the write
    // end above cSyncPipeFD so the dup2() below always targets a different fd.
    int writeFD = fcntl(pipeFDs[1], F_DUPFD_CLOEXEC, cSyncPipeFD + 1); // NOSONAR cpp:M23_058 - POSIX fd type

    static_cast<void>(close(pipeFDs[1]));

    if (writeFD < 0) {
        return Error(errno, "can't duplicate conmon sync pipe");
    }

    auto closeWrite = DeferRelease(&writeFD, CloseFD);

    // posix_spawn() takes argv and envp as non-const strings for historical reasons only: it doesn't modify them.
    std::vector<char*> argv;

    argv.reserve(args.size() + 1);

    static_cast<void>(std::transform(args.begin(), args.end(), std::back_inserter(argv), [](const std::string& arg) {
        return const_cast<char*>(arg.c_str()); // NOSONAR cpp:M23_090 - posix_spawn() doesn't modify argv
    }));

    argv.push_back(nullptr);

    // NOTIFY_SOCKET is meant for this process's own sd_notify() calls: crun would otherwise take it as the notify
    // socket of the container. Stale _OCI_* variables would confuse conmon's own pipe setup.
    const auto         syncPipeEnv = std::string(cSyncPipeEnv) + "=" + std::to_string(cSyncPipeFD);
    std::vector<char*> envp;

    for (char** var = environ; *var != nullptr; ++var) {
        if (const std::string_view value(*var); value.rfind("NOTIFY_SOCKET=", 0) == 0 || value.rfind("_OCI_", 0) == 0) {
            continue;
        }

        envp.push_back(*var);
    }

    envp.push_back(const_cast<char*>(syncPipeEnv.c_str())); // NOSONAR cpp:M23_090 - posix_spawn() doesn't modify envp
    envp.push_back(nullptr);

    posix_spawn_file_actions_t actions;

    if (auto rc = posix_spawn_file_actions_init(&actions); rc != 0) {
        return Error(rc, "can't init conmon spawn file actions");
    }

    auto destroyActions = DeferRelease(&actions, posix_spawn_file_actions_destroy);

    if (auto rc = posix_spawn_file_actions_adddup2(&actions, writeFD, cSyncPipeFD); rc != 0) {
        return Error(rc, "can't set up conmon sync pipe");
    }

    // The conmon monitor lives as long as the container: don't let it hold on to any other fd of this process that
    // happens to be inheritable, e.g. a pipe another thread is creating for its own child.
    if (auto err = AddCloseFrom(actions, cSyncPipeFD + 1); !err.IsNone()) {
        return err;
    }

    pid_t pid = -1;

    if (auto rc = posix_spawn(&pid, args[0].c_str(), &actions, nullptr, argv.data(), envp.data()); rc != 0) {
        return Error(rc, "can't spawn conmon");
    }

    int status = 0; // NOSONAR cpp:M23_058 - waitpid() status type

    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            return Error(errno, "can't wait for conmon");
        }
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return Error(ErrorEnum::eFailed,
            ("conmon failed: exit=" + std::to_string(WIFEXITED(status) ? WEXITSTATUS(status) : -1)).c_str());
    }

    syncFD = *closeRead.Release();

    return ErrorEnum::eNone;
}

// Reads the container creation result conmon reports on its sync pipe: {"data": <container pid>} on success or
// {"data": -1, "message": "..."} on failure. conmon exits without reporting anything when the runtime fails without
// any stderr output, which happens with the passthrough log driver since the runtime stderr isn't captured then.
RetWithError<pid_t> ReadSyncResult(int syncFD) // NOSONAR cpp:M23_058 - POSIX file descriptor type
{
    const auto deadline = std::chrono::steady_clock::now() + cCreateTimeout;

    std::string           data;
    std::array<char, 256> buffer {};

    while (data.find('\n') == std::string::npos) {
        const auto timeLeft
            = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (timeLeft.count() <= 0) {
            return {-1, Error(ErrorEnum::eTimeout, "timeout waiting for container creation")};
        }

        pollfd pfd {syncFD, POLLIN, 0};

        if (auto rc = poll(&pfd, 1, static_cast<int>(timeLeft.count())); rc <= 0) { // NOSONAR cpp:M23_058 - poll() API
            if (rc < 0 && errno != EINTR) {
                return {-1, Error(errno, "can't poll conmon sync pipe")};
            }

            continue;
        }

        const auto n = read(syncFD, buffer.data(), buffer.size());
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }

            return {-1, Error(errno, "can't read conmon sync pipe")};
        }

        if (n == 0) {
            return {-1, Error(ErrorEnum::eFailed, "container creation failed, see conmon log for details")};
        }

        data += std::string_view(buffer.data(), static_cast<size_t>(n));
    }

    try {
        auto object = Poco::JSON::Parser().parse(data.substr(0, data.find('\n'))).extract<Poco::JSON::Object::Ptr>();

        if (const auto pid = object->getValue<pid_t>("data"); pid > 0) {
            return pid;
        }

        return {-1,
            Error(ErrorEnum::eFailed, object->optValue<std::string>("message", "container creation failed").c_str())};
    } catch (const Poco::Exception& e) {
        return {-1, common::utils::ToAosError(e)};
    }
}

// Reports whether pid is a live conmon started with the given conmon pid file. A zombie has an empty cmdline, and
// matching the pid file, which is unique per instance, protects against the pid having been reused by any other
// process, including the conmon of another instance.
bool IsConmonAlive(pid_t pid, std::string_view conmonPIDFile)
{
    std::ifstream file("/proc/" + std::to_string(pid) + "/cmdline");
    std::string   prevArg;
    std::string   arg;

    while (std::getline(file, arg, '\0')) {
        if (prevArg == "--conmon-pidfile" && arg == conmonPIDFile) {
            return true;
        }

        prevArg.swap(arg);
    }

    return false;
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error CRunRunner::Init(const ContainerConfig& config)
{
    LOG_DBG() << "Initialize crun runner" << Log::Field("runtimeDir", config.mRuntimeDir.c_str())
              << Log::Field("stateRoot", config.mCRunStateRoot.c_str())
              << Log::Field("crunExecutable", config.mCRunExecutable.c_str())
              << Log::Field("conmonExecutable", config.mConmonExecutable.c_str())
              << Log::Field("conmonDir", config.mConmonDir.c_str());

    mRuntimeDir       = config.mRuntimeDir;
    mStateRoot        = config.mCRunStateRoot;
    mCRunExecutable   = config.mCRunExecutable;
    mConmonExecutable = config.mConmonExecutable;
    mConmonDir        = config.mConmonDir;

    return ErrorEnum::eNone;
}

Error CRunRunner::StartContainer(const std::string& instanceID)
{
    LOG_DBG() << "Start crun container" << Log::Field("instanceID", instanceID.c_str());

    // Refuse to touch an existing container: the failure cleanup below would otherwise destroy it.
    {
        libcrun_error_t err = nullptr;

        if (libcrun_container_status_t crunStatus = {};
            libcrun_read_container_status(&crunStatus, mStateRoot.c_str(), instanceID.c_str(), &err) == 0) {
            libcrun_free_container_status(&crunStatus);

            return AOS_ERROR_WRAP(Error(ErrorEnum::eAlreadyExist, "container already exists"));
        }

        static_cast<void>(libcrun_error_release(&err));
    }

    if (auto err = LaunchContainer(instanceID); !err.IsNone()) {
        if (auto removeErr = RemoveContainer(instanceID); !removeErr.IsNone() && !removeErr.Is(ErrorEnum::eNotFound)) {
            LOG_WRN() << "Failed to remove container" << Log::Field("instanceID", instanceID.c_str())
                      << Log::Field(removeErr);
        }

        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

RetWithError<ContainerStatus> CRunRunner::GetContainerStatus(const std::string& instanceID)
{
    LOG_DBG() << "Get crun container status" << Log::Field("instanceID", instanceID.c_str());

    return GetStatus(instanceID);
}

RetWithError<std::vector<ContainerStatus>> CRunRunner::ListContainers()
{
    // Enumerate crun's own state root instead of keeping a separate in-memory set of managed instances: this
    // stays correct even for containers this object did not itself start (WatchInstance re-attaching after
    // a restart) and can never drift from what crun itself considers alive. mStateRoot is exclusively ours
    // (libcrun_get_containers_list() fails outright if the directory holds anything crun didn't create), so
    // every entry found here is, by construction, a container this runner manages - never a foreign one.
    libcrun_error_t           err  = nullptr;
    libcrun_container_list_t* list = nullptr;

    if (libcrun_get_containers_list(&list, mStateRoot.c_str(), &err) < 0) {
        auto aosErr = ReleaseLibcrunError(err);
        if (aosErr.Is(ErrorEnum::eNotFound)) {
            return {{}};
        }

        return {{}, AOS_ERROR_WRAP(aosErr)};
    }

    auto release = DeferRelease(list, libcrun_free_containers_list);

    std::vector<ContainerStatus> result;

    for (auto* entry = list; entry != nullptr; entry = entry->next) {
        auto [status, statusErr] = GetStatus(entry->name);
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

    Error             aosErr;
    libcrun_error_t   err = nullptr;
    libcrun_context_t ctx = MakeContext(mStateRoot, instanceID);

    if (libcrun_container_delete(&ctx, nullptr, instanceID.c_str(), true, &err) < 0) {
        aosErr = AOS_ERROR_WRAP(ReleaseLibcrunError(err));
    }

    // conmon writes the exit file once it has reaped the container: let it finish before its persist dir is removed,
    // otherwise it could write a stale exit file into the persist dir of the next container started for the instance.
    WaitConmonExit(instanceID);

    std::error_code ec;

    static_cast<void>(std::filesystem::remove_all(GetPersistDir(instanceID), ec));

    if (ec) {
        LOG_WRN() << "Failed to remove conmon persist dir" << Log::Field("instanceID", instanceID.c_str())
                  << Log::Field("error", ec.message().c_str());
    }

    return aosErr;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

RetWithError<ContainerStatus> CRunRunner::GetStatus(const std::string& instanceID) const
{
    ContainerStatus status;

    status.mInstanceID = instanceID;
    status.mState      = InstanceStateEnum::eActive;

    libcrun_error_t            err        = nullptr;
    libcrun_container_status_t crunStatus = {};

    if (libcrun_read_container_status(&crunStatus, mStateRoot.c_str(), instanceID.c_str(), &err) < 0) {
        libcrun_error_release(&err);

        status.mState = InstanceStateEnum::eFailed;
        ReadExitStatus(instanceID, status);

        return {status, ErrorEnum::eNone};
    }

    const int running = libcrun_is_container_running(&crunStatus, &err);

    libcrun_free_container_status(&crunStatus);

    if (running <= 0) {
        libcrun_error_release(&err);

        status.mState = InstanceStateEnum::eFailed;
        ReadExitStatus(instanceID, status);
    }

    return {status, ErrorEnum::eNone};
}

void CRunRunner::ReadExitStatus(const std::string& instanceID, ContainerStatus& status) const
{
    const auto persistDir = GetPersistDir(instanceID);

    // The exit file is missing until conmon has reaped the container, or when conmon itself is gone (e.g. killed
    // along with a previous SM instance): the exit code is unknown then.
    std::ifstream exitFile(common::utils::JoinPath(persistDir, cExitFile));

    if (int32_t exitCode = 0; exitFile >> exitCode) {
        status.mExitCode.SetValue(exitCode);
    }

    std::error_code ec;

    status.mOOMKilled = std::filesystem::exists(common::utils::JoinPath(persistDir, cOOMFile), ec);
}

Error CRunRunner::LaunchContainer(const std::string& instanceID) const
{
    // Kept as std::string: it's passed to conmon as an argument and joined with common::utils::JoinPath.
    const auto persistDir = GetPersistDir(instanceID); // NOSONAR cpp:S6010 - see above

    std::error_code ec;

    // Drop leftovers of a previous run, so a stale exit or oom file can't be attributed to this container.
    static_cast<void>(std::filesystem::remove_all(persistDir, ec));

    if (ec) {
        return Error(ErrorEnum::eFailed, ("can't remove conmon persist dir: " + ec.message()).c_str());
    }

    if (!std::filesystem::create_directories(persistDir, ec) && ec) {
        return Error(ErrorEnum::eFailed, ("can't create conmon persist dir: " + ec.message()).c_str());
    }

    // conmon runs "crun create" and then stays alive as the container's subreaper, recording the container exit code
    // into <persistDir>/exit and an OOM kill into <persistDir>/oom. The passthrough log driver lets the container
    // inherit this process's stdout/stderr, so container output goes to the same place as before.
    const std::vector<std::string> args = {
        mConmonExecutable,
        "--api-version",
        "--cid",
        instanceID,
        "--cuuid",
        instanceID,
        "--runtime",
        mCRunExecutable,
        "--runtime-arg=--root=" + mStateRoot,
        "--bundle",
        common::utils::JoinPath(mRuntimeDir, instanceID),
        "--container-pidfile",
        common::utils::JoinPath(persistDir, cContainerPIDFile),
        "--conmon-pidfile",
        common::utils::JoinPath(persistDir, cConmonPIDFile),
        "--persist-dir",
        persistDir,
        "--log-path",
        "passthrough",
        "--syslog",
    };

    int syncFD = -1; // NOSONAR cpp:M23_058 - POSIX file descriptor type

    if (auto err = SpawnConmon(args, syncFD); !err.IsNone()) {
        return err;
    }

    auto closeSync = DeferRelease(&syncFD, CloseFD);

    auto [pid, err] = ReadSyncResult(syncFD);
    if (!err.IsNone()) {
        return err;
    }

    LOG_DBG() << "Crun container created" << Log::Field("instanceID", instanceID.c_str()) << Log::Field("pid", pid);

    libcrun_error_t crunErr = nullptr;

    if (auto ctx = MakeContext(mStateRoot, instanceID);
        libcrun_container_start(&ctx, instanceID.c_str(), &crunErr) < 0) {
        return ReleaseLibcrunError(crunErr);
    }

    return ErrorEnum::eNone;
}

void CRunRunner::WaitConmonExit(const std::string& instanceID) const
{
    const auto    conmonPIDFile = common::utils::JoinPath(GetPersistDir(instanceID), cConmonPIDFile);
    std::ifstream pidFile(conmonPIDFile);
    pid_t         pid = 0;

    if (!(pidFile >> pid) || pid <= 0) {
        return;
    }

    const auto deadline = std::chrono::steady_clock::now() + cConmonExitTimeout;

    while (IsConmonAlive(pid, conmonPIDFile)) {
        if (std::chrono::steady_clock::now() >= deadline) {
            LOG_WRN() << "Conmon didn't exit in time, kill it" << Log::Field("instanceID", instanceID.c_str())
                      << Log::Field("pid", pid);

            if (kill(pid, SIGKILL) != 0) {
                const Error err(errno, "can't kill conmon");

                LOG_WRN() << "Failed to kill conmon" << Log::Field("instanceID", instanceID.c_str()) << Log::Field(err);
            }

            return;
        }

        std::this_thread::sleep_for(cConmonExitPollPeriod);
    }
}

std::string CRunRunner::GetPersistDir(const std::string& instanceID) const
{
    return common::utils::JoinPath(mConmonDir, instanceID);
}

} // namespace aos::sm::launcher
