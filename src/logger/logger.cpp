/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <chrono>
#include <ctime>
#include <functional>
#include <iomanip>
#include <iostream>

#include "logger.hpp"

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

std::mutex      Logger::sMutex;
bool            Logger::sColored = true;
Logger::Backend Logger::sBackend = Logger::Backend::eStdIO;

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

aos::Error Logger::Init()
{
    switch (sBackend) {
    case Backend::eStdIO:
        SetColored(true);
        aos::Log::SetCallback(Logger::StdIOCallback);
    }

    return aos::ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

void Logger::StdIOCallback(aos::LogModule module, aos::LogLevel level, const aos::String& message)
{
    std::lock_guard lock(sMutex);

    std::cout << GetCurrentTime() << " " << GetLogLevel(level) << " " << GetModule(module) << " " << message.CStr()
              << std::endl;
}

std::string Logger::GetCurrentTime()
{
    auto              now       = std::chrono::system_clock::now();
    auto              ms        = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    auto              time      = std::chrono::system_clock::to_time_t(now);
    auto              localTime = *std::localtime(&time);
    std::stringstream ss;

    ss << (sColored ? cColorTime : "") << std::put_time(&localTime, "%d.%m.%y %H:%M:%S") << "." << std::setfill('0')
       << std::setw(3) << ms.count() << (sColored ? cColorNone : "");

    return ss.str();
}

std::string Logger::GetLogLevel(aos::LogLevel level)
{
    std::stringstream ss;

    switch (static_cast<aos::LogLevelEnum>(level)) {
    case aos::LogLevelEnum::eDebug:
        ss << (sColored ? cColorDebug : "") << "[DBG]";
        break;

    case aos::LogLevelEnum::eInfo:
        ss << (sColored ? cColorInfo : "") << "[INF]";
        break;

    case aos::LogLevelEnum::eWarning:
        ss << (sColored ? cColorWarning : "") << "[WRN]";
        break;

    case aos::LogLevelEnum::eError:
        ss << (sColored ? cColorError : "") << "[ERR]";
        break;

    default:
        ss << (sColored ? cColorUnknown : "") << "[UNK]";
    }

    ss << (sColored ? cColorNone : "");

    return ss.str();
}

std::string Logger::GetModule(aos::LogModule module)
{
    std::stringstream ss;

    ss << (sColored ? cColorModule : "") << "(";

    if (module.GetValue() >= AosLogModule(LogModuleEnum::eNumModules)) {
        ss << "unknown";
    } else if (module.GetValue() >= aos::LogModuleEnum::eNumModules) {
        ss << LogModule(IAMLogModule(module.GetValue())).ToString().CStr();
    } else {
        ss << module.ToString().CStr();
    }

    ss << ")" << (sColored ? cColorNone : "");

    return ss.str();
}
