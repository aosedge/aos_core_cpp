/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LOGMODULE_HPP_
#define LOGMODULE_HPP_

#include <aos/common/tools/log.hpp>

#include <logger/logger.hpp>

/**
 * Log module type.
 */
class LogModuleType {
public:
    enum class Enum {
        eApp,
        eConfig,
        eDatabase,
        eVISIdentifier,
        eIAMClient,
        eIAMServer,
        eUtils,
        eNumModules,
    };

    static const aos::Array<const char* const> GetStrings()
    {
        static const char* const sLogModuleTypeStrings[] = {
            "app",
            "config",
            "database",
            "visidentifier",
            "iamclient",
            "iamserver",
            "utils",
        };

        return aos::Array<const char* const>(sLogModuleTypeStrings, aos::ArraySize(sLogModuleTypeStrings));
    };
};

using LogModuleEnum = LogModuleType::Enum;
using LogModule     = aos::EnumStringer<LogModuleType>;

/**
 * Converts IAM LogModuleEnum to aos::LogModuleEnum.
 *
 * @param module IAM log module.
 * @return aos::LogModuleEnum.
 */
constexpr auto AosLogModule(LogModuleEnum module)
{
    return static_cast<aos::LogModuleEnum>(
        static_cast<int>(aos::LogModuleEnum::eNumModules) + static_cast<int>(module));
}

/**
 * Converts aos::LogModuleEnum to IAM LogModuleEnum.
 *
 * @param module Aos log module.
 * @return aos::LogModuleEnum.
 */
constexpr auto IAMLogModule(aos::LogModuleEnum module)
{
    return static_cast<LogModuleEnum>(static_cast<int>(module) - static_cast<int>(aos::LogModuleEnum::eNumModules));
}

class IAMLogger : public aos::common::logger::Logger {
private:
    std::string GetModule(aos::LogModule module) override
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
};

#endif
