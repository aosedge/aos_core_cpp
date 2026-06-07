/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_PROCESS_PROCESSSPAWNER_HPP_
#define AOS_COMMON_PROCESS_PROCESSSPAWNER_HPP_

#include <core/common/tools/noncopyable.hpp>

#include "itf/processspawner.hpp"

namespace aos::common::process {

/**
 * Poco-backed ProcessSpawnerItf implementation.
 *
 * Spawn uses Poco::Process::launch; Signal/Kill use ::kill plus ::waitpid for
 * the reap. IsAlive uses Poco::Process::isRunning. Kill tolerates ESRCH
 * ("already gone") and ECHILD ("not our child" — the process was adopted from
 * a previous lifetime and has been reparented to init); both return eNone.
 */
class PocoProcessSpawner : public ProcessSpawnerItf, private aos::NonCopyable {
public:
    /**
     * Launches a binary.
     *
     * @param binary absolute path to the executable.
     * @param args argv tail (argv[0] is supplied automatically).
     * @return RetWithError<Poco::Process::PID>.
     */
    RetWithError<Poco::Process::PID> Spawn(const std::string& binary, const std::vector<std::string>& args) override;

    /**
     * Terminates the process and reaps the zombie.
     *
     * @param pid target PID.
     * @return Error.
     */
    Error Kill(Poco::Process::PID pid) override;

    /**
     * Sends a signal to the process.
     *
     * @param pid target PID.
     * @param signum signal number.
     * @return Error.
     */
    Error Signal(Poco::Process::PID pid, int signum) override;

    /**
     * Checks whether the process is currently running.
     *
     * @param pid target PID.
     * @return bool.
     */
    bool IsAlive(Poco::Process::PID pid) const override;

    /**
     * Reads /proc/<pid>/cmdline and returns it with NUL separators replaced by spaces.
     *
     * @param pid target PID.
     * @return RetWithError<std::string>.
     */
    RetWithError<std::string> GetCmdLine(Poco::Process::PID pid) const override;
};

} // namespace aos::common::process

#endif
