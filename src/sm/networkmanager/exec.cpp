/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

#include <Poco/Exception.h>
#include <Poco/Pipe.h>
#include <Poco/PipeStream.h>
#include <Poco/Process.h>

#include <common/utils/exception.hpp>
#include <common/utils/json.hpp>
#include <common/utils/parser.hpp>

#include "exec.hpp"

namespace aos::sm::cni {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

namespace {

Poco::Process::Env PrepareEnv(const std::string& args)
{
    Poco::Process::Env env;
    std::istringstream iss(args);
    std::string        token;

    while (std::getline(iss, token, ' ')) {
        if (!token.empty()) {
            if (auto keyValue = aos::common::utils::ParseKeyValue(token, true, "="); keyValue.has_value()) {
                env[keyValue->mKey] = keyValue->mValue;
            }
        }
    }

    return env;
}

std::string PluginErr(
    const std::string& stderrContent, const std::string& stdoutContent, const std::string& defaultMessage)
{
    if (stdoutContent.empty()) {
        if (stderrContent.empty()) {
            return defaultMessage;
        } else {
            return "plugin failed: " + stderrContent;
        }
    }

    auto [var, err] = common::utils::ParseJson(stdoutContent);
    if (err.IsNone()) {
        Poco::JSON::Object::Ptr                     object = var.extract<Poco::JSON::Object::Ptr>();
        common::utils::CaseInsensitiveObjectWrapper wrapper(object);

        if (wrapper.Has("msg")) {
            return "plugin failed: " + wrapper.GetValue<std::string>("msg");
        }
    }

    return "plugin failed: " + stdoutContent;
}

std::string LaunchPlugin(
    const std::string& payload, const std::string& pluginPath, Poco::Process::Env& env, int maxRetries = 5)
{
    Poco::Pipe          inPipe, outPipe, errPipe;
    Poco::Process::Args processArgs;
    std::string         output, error;

    for (int i = 1; i <= maxRetries; ++i) {
        Poco::ProcessHandle ph = Poco::Process::launch(pluginPath, processArgs, &inPipe, &outPipe, &errPipe, env);

        Poco::PipeOutputStream ostr(inPipe);
        ostr << payload;
        ostr.close();
        inPipe.close();

        output.clear();
        error.clear();

        Poco::PipeInputStream istr(outPipe);
        std::string           line;
        while (std::getline(istr, line)) {
            output += line + "\n";
        }

        Poco::PipeInputStream estr(errPipe);
        while (std::getline(estr, line)) {
            error += line + "\n";
        }

        if (auto exitCode = ph.wait(); exitCode != 0) {
            if (i < maxRetries && error.find("text file busy") != std::string::npos) {
                std::this_thread::sleep_for(std::chrono::seconds(1));

                continue;
            }

            throw std::runtime_error(
                PluginErr(error, output, "plugin execution failed with exit code " + std::to_string(exitCode)));
        }

        return output;
    }

    throw std::runtime_error("max retries exceeded for plugin execution.");
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

RetWithError<std::string> Exec::ExecPlugin(
    const std::string& payload, const std::string& pluginPath, const std::string& args) const
{
    try {
        Poco::Process::Env env = PrepareEnv(args);

        return LaunchPlugin(payload, pluginPath, env);
    } catch (const std::exception& e) {
        return {"", AOS_ERROR_WRAP(common::utils::ToAosError(e))};
    }
}

} // namespace aos::sm::cni
