/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sstream>

#include <Poco/Pipe.h>
#include <Poco/PipeStream.h>
#include <Poco/Process.h>
#include <Poco/StreamCopier.h>
#include <Poco/String.h>
#include <Poco/UUID.h>
#include <Poco/UUIDGenerator.h>

#include "utils.hpp"

namespace aos::common::utils {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

RetWithError<std::string> ExecCommand(const std::vector<std::string>& args)
{
    if (args.empty()) {
        return {"", Error(ErrorEnum::eInvalidArgument, "exec command requires at least one argument")};
    }

    const std::string   program = args[0];
    Poco::Pipe          outPipe;
    Poco::Process::Args pocoArgs;

    for (size_t i = 1; i < args.size(); ++i) {
        pocoArgs.push_back(args[i]);
    }

    Poco::ProcessHandle   ph = Poco::Process::launch(program, pocoArgs, nullptr, &outPipe, &outPipe);
    Poco::PipeInputStream istr(outPipe);
    std::ostringstream    output;

    Poco::StreamCopier::copyStream(istr, output);

    if (int rc = ph.wait(); rc != 0) {
        std::ostringstream err;

        err << "command `" << program;

        for (const auto& a : pocoArgs) {
            err << ' ' << a;
        }

        err << "` failed (exit=" << rc << "):" << output.str();

        return {"", Error(ErrorEnum::eRuntime, err.str().c_str())};
    }

    return {output.str(), ErrorEnum::eNone};
}

std::string NameUUID(const std::string& name)
{
    auto& generator = Poco::UUIDGenerator::defaultGenerator();

    return generator.createFromName(Poco::UUID::oid(), name).toString();
}

} // namespace aos::common::utils
