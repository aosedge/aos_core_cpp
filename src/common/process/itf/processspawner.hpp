/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_PROCESS_ITF_PROCESSSPAWNER_HPP_
#define AOS_COMMON_PROCESS_ITF_PROCESSSPAWNER_HPP_

#include <string>
#include <vector>

#include <Poco/Process.h>

#include <core/common/tools/error.hpp>

namespace aos::common::process {

/**
 * Process-spawn seam.
 */
class ProcessSpawnerItf {
public:
    /**
     * Destructor.
     */
    virtual ~ProcessSpawnerItf() = default;

    /**
     * Launches a binary.
     *
     * @param binary absolute path to the executable.
     * @param args argv tail (argv[0] is supplied automatically).
     * @return RetWithError<Poco::Process::PID>.
     */
    virtual RetWithError<Poco::Process::PID> Spawn(const std::string& binary, const std::vector<std::string>& args) = 0;

    /**
     * Terminates the process and reaps it.
     *
     * @param pid target PID.
     * @return Error.
     */
    virtual Error Kill(Poco::Process::PID pid) = 0;

    /**
     * Sends a signal to the process.
     *
     * @param pid target PID.
     * @param signum signal number.
     * @return Error.
     */
    virtual Error Signal(Poco::Process::PID pid, int signum) = 0;

    /**
     * Checks whether the process is currently running.
     *
     * @param pid target PID.
     * @return bool.
     */
    virtual bool IsAlive(Poco::Process::PID pid) const = 0;

    /**
     * Returns the process command line (argv joined by spaces).
     *
     * @param pid target PID.
     * @return RetWithError<std::string>.
     */
    virtual RetWithError<std::string> GetCmdLine(Poco::Process::PID pid) const = 0;
};

} // namespace aos::common::process

#endif
