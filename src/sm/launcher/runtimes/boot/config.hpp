/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_LAUNCHER_RUNTIMES_BOOT_CONFIG_HPP_
#define AOS_SM_LAUNCHER_RUNTIMES_BOOT_CONFIG_HPP_

#include <optional>
#include <string>
#include <vector>

#include <core/common/tools/time.hpp>
#include <core/common/types/common.hpp>

#include <common/utils/json.hpp>
#include <sm/config/config.hpp>

namespace aos::sm::launcher {

class BootDetectModeType {
public:
    enum class Enum {
        eNone,
        eAuto,
    };

    static const Array<const char* const> GetStrings()
    {
        static const char* const sStrings[] = {
            "",
            "auto",
        };

        return Array<const char* const>(sStrings, ArraySize(sStrings));
    };
};

using BootDetectModeEnum = BootDetectModeType::Enum;
using BootDetectMode     = EnumStringer<BootDetectModeType>;

/**
 * Boot runtime config.
 */
struct BootConfig {
    std::string              mWorkingDir;
    std::string              mLoader;
    BootDetectMode           mDetectMode;
    std::string              mVersionFile;
    std::vector<std::string> mPartitions;
    std::vector<std::string> mHealthCheckServices;
};

/**
 * Parses boot runtime config.
 *
 * @param config runtime config.
 * @param[out] bootConfig boot runtime config.
 */
Error ParseConfig(const RuntimeConfig& config, BootConfig& bootConfig);

} // namespace aos::sm::launcher

#endif
