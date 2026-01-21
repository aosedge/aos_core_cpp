/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <common/utils/exception.hpp>
#include <common/utils/filesystem.hpp>

#include "config.hpp"

namespace aos::sm::launcher {

namespace {

/***********************************************************************************************************************
 * Consts
 **********************************************************************************************************************/

constexpr auto cDefaultBootRuntimeDir  = "runtimes/boot";
constexpr auto cDefaultBootVersionFile = "aos/version";

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error ParseConfig(const RuntimeConfig& config, BootConfig& bootConfig)
{
    try {
        const auto object = common::utils::CaseInsensitiveObjectWrapper(config.mConfig);

        bootConfig.mWorkingDir = object.GetValue<std::string>(
            "workingDir", common::utils::JoinPath(config.mWorkingDir, cDefaultBootRuntimeDir));
        bootConfig.mLoader      = object.GetValue<std::string>("loader");
        bootConfig.mVersionFile = object.GetValue<std::string>("versionFile", cDefaultBootVersionFile);

        auto err = bootConfig.mDetectMode.FromString(object.GetValue<std::string>("detectMode").c_str());
        AOS_ERROR_CHECK_AND_THROW(err, "invalid detect mode in boot runtime config");

        bootConfig.mPartitions = common::utils::GetArrayValue<std::string>(
            object, "partitions", [](const Poco::Dynamic::Var& value) { return value.convert<std::string>(); });
        bootConfig.mHealthCheckServices = common::utils::GetArrayValue<std::string>(object, "healthCheckServices",
            [](const Poco::Dynamic::Var& value) { return value.convert<std::string>(); });
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::sm::launcher
