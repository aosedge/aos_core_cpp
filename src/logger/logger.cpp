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

#include <systemd/sd-journal.h>

#include "logger/logger.hpp"

namespace aos::common::logger {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

std::mutex      Logger::sMutex;
bool            Logger::sColored  = true;
Logger::Backend Logger::sBackend  = Logger::Backend::eStdIO;
aos::LogLevel   Logger::sLogLevel = aos::LogLevelEnum::eInfo;
Logger*         Logger::mInstance {};

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

aos::Error Logger::Init()
{
    std::lock_guard lock(sMutex);

    mInstance = this;

    switch (sBackend) {
    case Backend::eStdIO:
        SetColored(true);
        aos::Log::SetCallback(Logger::StdIOCallback);

        break;

    case Backend::eJournald:
        SetColored(false);
        aos::Log::SetCallback(Logger::JournaldCallback);

        break;
    }

    return aos::ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

void Logger::StdIOCallback(aos::LogModule module, aos::LogLevel level, const aos::String& message)
{
    std::lock_guard lock(sMutex);

    if (level.GetValue() < sLogLevel.GetValue()) {
        return;
    }

    std::cout << mInstance->GetCurrentTime() << " " << mInstance->GetLogLevel(level) << " "
              << mInstance->GetModule(module) << " " << message.CStr() << std::endl;
}

void Logger::JournaldCallback(aos::LogModule module, aos::LogLevel level, const aos::String& message)
{
    if (level.GetValue() < sLogLevel.GetValue()) {
        return;
    }

    std::stringstream ss;

    ss << mInstance->GetModule(module) << " " << message.CStr();

    auto ret = sd_journal_print(GetSyslogPriority(level), "%s", ss.str().c_str());
    if (ret != 0) {
        std::cerr << "Can't write to journal: " << ret;
    }
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

    ss << (sColored ? cColorModule : "") << "(" << module.ToString().CStr() << ")" << (sColored ? cColorNone : "");

    return ss.str();
}

int Logger::GetSyslogPriority(aos::LogLevel level)
{
    switch (static_cast<aos::LogLevelEnum>(level)) {
    case aos::LogLevelEnum::eDebug:
        return LOG_DEBUG;

    case aos::LogLevelEnum::eInfo:
        return LOG_INFO;

    case aos::LogLevelEnum::eWarning:
        return LOG_WARNING;

    case aos::LogLevelEnum::eError:
        return LOG_ERR;

    default:
        return LOG_NOTICE;
    }
}

} // namespace aos::common::logger
