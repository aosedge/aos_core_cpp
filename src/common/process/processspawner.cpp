/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

#include <sys/wait.h>

#include <Poco/Exception.h>
#include <Poco/Process.h>

#include <core/common/tools/logger.hpp>

#include "processspawner.hpp"

namespace aos::common::process {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

RetWithError<Poco::Process::PID> PocoProcessSpawner::Spawn(
    const std::string& binary, const std::vector<std::string>& args)
{
    LOG_DBG() << "Spawn process" << Log::Field("binary", binary.c_str());

    try {
        Poco::Process::Args pocoArgs(args.begin(), args.end());

        auto handle = Poco::Process::launch(binary, pocoArgs);

        return {handle.id(), ErrorEnum::eNone};
    } catch (const Poco::Exception& e) {
        return {0, Error(ErrorEnum::eRuntime, e.displayText().c_str())};
    }
}

Error PocoProcessSpawner::Kill(Poco::Process::PID pid)
{
    LOG_DBG() << "Kill process" << Log::Field("pid", pid);

    // Kill tolerates ESRCH: a missing process is the requested end state, not an error.
    if (auto err = Signal(pid, SIGTERM); !err.IsNone() && !err.Is(ErrorEnum::eNotFound)) {
        return err;
    }

    int status = 0;

    if (::waitpid(pid, &status, 0) < 0 && errno != ECHILD) {
        return Error(ErrorEnum::eRuntime, std::strerror(errno));
    }

    return ErrorEnum::eNone;
}

Error PocoProcessSpawner::Signal(Poco::Process::PID pid, int signum)
{
    if (::kill(pid, signum) != 0) {
        if (errno == ESRCH) {
            return Error(ErrorEnum::eNotFound, "process not found");
        }

        return Error(ErrorEnum::eRuntime, std::strerror(errno));
    }

    return ErrorEnum::eNone;
}

bool PocoProcessSpawner::IsAlive(Poco::Process::PID pid) const
{
    return Poco::Process::isRunning(pid);
}

RetWithError<std::string> PocoProcessSpawner::GetCmdLine(Poco::Process::PID pid) const
{
    const auto path = "/proc/" + std::to_string(pid) + "/cmdline";

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return {std::string {}, Error(ErrorEnum::eNotFound, "cmdline not readable")};
    }

    std::string content((std::istreambuf_iterator<char>(file)), {});

    std::replace(content.begin(), content.end(), '\0', ' ');

    return {std::move(content), ErrorEnum::eNone};
}

} // namespace aos::common::process
