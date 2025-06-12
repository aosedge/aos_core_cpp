/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LOGGER_HPP_
#define LOGGER_HPP_

#include <mutex>

#include <aos/common/tools/log.hpp>

namespace aos::common::logger {

/**
 * Logger instance.
 */
class Logger {
public:
    /**
     * Log backends.
     */
    enum class Backend {
        eStdIO,
        eJournald,
    };

    /**
     * Initializes logging system.
     *
     * @return aos::Error.
     */
    aos::Error Init();

    /**
     * Sets logger backend.
     *
     * @param backend logger backend.
     */
    void SetBackend(Backend backend)
    {
        std::lock_guard lock(sMutex);

        sBackend = backend;
    }

    /**
     * Sets current log level.
     *
     * @param level log level.
     */
    void SetLogLevel(aos::LogLevel level)
    {
        std::lock_guard lock(sMutex);

        sLogLevel = level;
    }

protected:
    static constexpr auto cColorModule  = "\033[34m";
    static constexpr auto cColorNone    = "\033[0m";
    static constexpr auto cColorTime    = "\033[90m";
    static constexpr auto cColorDebug   = "\033[37m";
    static constexpr auto cColorInfo    = "\033[32m";
    static constexpr auto cColorWarning = "\033[33m";
    static constexpr auto cColorError   = "\033[31m";
    static constexpr auto cColorUnknown = "\033[36m";

    static bool sColored;

private:
    static void StdIOCallback(const String& module, aos::LogLevel level, const aos::String& message);
    static void JournaldCallback(const String& module, aos::LogLevel level, const aos::String& message);

    static void        SetColored(bool colored) { sColored = colored; }
    static int         GetSyslogPriority(aos::LogLevel level);
    static std::string GetModule(const String& module);

    virtual std::string GetCurrentTime();
    virtual std::string GetLogLevel(aos::LogLevel level);

    static std::mutex    sMutex;
    static Backend       sBackend;
    static aos::LogLevel sLogLevel;
    static Logger*       mInstance;
};

} // namespace aos::common::logger

#endif
