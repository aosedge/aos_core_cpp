/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <sstream>
#include <string_view>

#include <Poco/Base64Decoder.h>
#include <Poco/Base64Encoder.h>
#include <Poco/String.h>
#include <Poco/UUID.h>
#include <Poco/UUIDGenerator.h>

#include "utils.hpp"

extern char** environ;

namespace aos::common::utils {

namespace {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

// Builds argv/envp arrays for posix_spawn. NOTIFY_SOCKET is excluded from the environment: it's meant for
// aos_sm_app's own sd_notify() calls, and a spawned OCI runtime (crun) reacts to an inherited NOTIFY_SOCKET by
// trying to set up a notify-socket proxy inside the container's rootfs, which fails there. Building an explicit
// envp per call (rather than mutating the process-wide environment) needs no locking, so concurrent callers
// spawn fully in parallel.
class SpawnArgs {
public:
    explicit SpawnArgs(const std::vector<std::string>& args)
    {
        for (const auto& arg : args) {
            mArgv.push_back(const_cast<char*>(arg.c_str()));
        }
        mArgv.push_back(nullptr);

        for (char** var = environ; *var != nullptr; ++var) {
            if (std::string_view(*var).rfind("NOTIFY_SOCKET=", 0) == 0) {
                continue;
            }

            mEnvp.push_back(*var);
        }
        mEnvp.push_back(nullptr);
    }

    char* const* Argv() { return mArgv.data(); }
    char* const* Envp() { return mEnvp.data(); }

private:
    std::vector<char*> mArgv;
    std::vector<char*> mEnvp;
};

Error SpawnProcess(const std::vector<std::string>& args, const posix_spawn_file_actions_t* fileActions, pid_t& pid)
{
    if (args.empty()) {
        return Error(ErrorEnum::eInvalidArgument, "exec command requires at least one argument");
    }

    SpawnArgs spawnArgs(args);

    if (posix_spawnp(&pid, args[0].c_str(), fileActions, nullptr, spawnArgs.Argv(), spawnArgs.Envp()) != 0) {
        return Error(ErrorEnum::eRuntime, "can't spawn process");
    }

    return ErrorEnum::eNone;
}

int WaitForExitCode(pid_t pid)
{
    int status = 0;

    waitpid(pid, &status, 0);

    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

Error CheckExitCode(int rc, const std::string& output, const std::initializer_list<int>& expectedExitCodes)
{
    if (std::find(expectedExitCodes.begin(), expectedExitCodes.end(), rc) != expectedExitCodes.end()) {
        return ErrorEnum::eNone;
    }

    std::ostringstream err;

    err << "exit=" << rc;

    if (!output.empty()) {
        constexpr size_t cMaxOutputLen = 128;
        err << ": " << output.substr(0, cMaxOutputLen);
        if (output.size() > cMaxOutputLen) {
            err << "...";
        }
    }

    return Error(ErrorEnum::eRuntime, err.str().c_str());
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

RetWithError<std::string> ExecCommand(
    const std::vector<std::string>& args, const std::initializer_list<int>& expectedExitCodes)
{
    int pipeFDs[2];

    if (pipe(pipeFDs) != 0) {
        return {"", Error(ErrorEnum::eRuntime, "can't create pipe")};
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, pipeFDs[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, pipeFDs[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, pipeFDs[0]);
    posix_spawn_file_actions_addclose(&actions, pipeFDs[1]);

    pid_t pid      = -1;
    Error spawnErr = SpawnProcess(args, &actions, pid);

    posix_spawn_file_actions_destroy(&actions);
    close(pipeFDs[1]);

    if (!spawnErr.IsNone()) {
        close(pipeFDs[0]);

        return {"", spawnErr};
    }

    // Wait for the launched command to exit before reading its output. A detached grandchild (e.g. a
    // daemonized container process started via "crun run -d") can inherit the pipe's write end and keep
    // it open indefinitely, so reading until EOF could block forever even though the launched command
    // itself has already finished. Once it's exited, drain only what's already buffered.
    const int rc = WaitForExitCode(pid);

    std::string outStr;
    char        buffer[1024];

    for (;;) {
        pollfd pfd {pipeFDs[0], POLLIN, 0};

        if (poll(&pfd, 1, 0) <= 0 || !(pfd.revents & POLLIN)) {
            break;
        }

        const int n = read(pipeFDs[0], buffer, sizeof(buffer));
        if (n <= 0) {
            break;
        }

        outStr.append(buffer, n);
    }

    close(pipeFDs[0]);

    return {outStr, CheckExitCode(rc, outStr, expectedExitCodes)};
}

Error ExecDetachedCommand(const std::vector<std::string>& args, const std::initializer_list<int>& expectedExitCodes)
{
    // No pipe/file actions here: the spawned process inherits this process's actual stdout/stderr (the
    // journal, for aos_sm_app). This is for commands that daemonize a long-running process expected to keep
    // producing output for its whole lifetime (e.g. "crun run -d"), which a pipe capturing just the launcher
    // command's own short-lived setup output isn't suited for.
    pid_t pid = -1;

    if (auto err = SpawnProcess(args, nullptr, pid); !err.IsNone()) {
        return err;
    }

    return CheckExitCode(WaitForExitCode(pid), "", expectedExitCodes);
}

std::string NameUUID(const std::string& name)
{
    auto& generator = Poco::UUIDGenerator::defaultGenerator();

    return generator.createFromName(Poco::UUID::oid(), name).toString();
}

std::string Base64Decode(const std::string& encoded)
{
    std::istringstream  encodedStream(encoded);
    Poco::Base64Decoder decoder(encodedStream);

    return std::string(std::istreambuf_iterator<char>(decoder), std::istreambuf_iterator<char>());
}

std::string Base64Encode(std::string_view decoded)
{
    std::ostringstream  encodedStream;
    Poco::Base64Encoder encoder(encodedStream);

    encoder << decoded;
    encoder.close();

    return encodedStream.str();
}

} // namespace aos::common::utils
