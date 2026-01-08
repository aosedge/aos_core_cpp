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

constexpr auto cDefaultRootfsRuntimeDir  = "runtimes/rootfs";
constexpr auto cDefaultRootfsVersionFile = "/etc/aos/version";

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error ParseConfig(const RuntimeConfig& config, RootfsConfig& rootfsConfig)
{
    try {
        const auto object = common::utils::CaseInsensitiveObjectWrapper(config.mConfig);

        rootfsConfig.mWorkingDir = object.GetValue<std::string>(
            "workingDir", common::utils::JoinPath(config.mWorkingDir, cDefaultRootfsRuntimeDir));
        rootfsConfig.mVersionFilePath     = object.GetValue<std::string>("versionFilePath", cDefaultRootfsVersionFile);
        rootfsConfig.mHealthCheckServices = common::utils::GetArrayValue<std::string>(object, "healthCheckServices",
            [](const Poco::Dynamic::Var& value) { return value.convert<std::string>(); });
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::sm::launcher
