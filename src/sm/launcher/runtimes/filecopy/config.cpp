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

constexpr auto cDefaultTargetBaseDir = "/var/aos/components";
constexpr auto cDefaultRuntimeSubDir = "runtimes";

} // namespace

Error ParseConfig(const RuntimeConfig& config, FileCopyConfig& fileCopyConfig)
{
    try {
        const auto object = common::utils::CaseInsensitiveObjectWrapper(config.mConfig);

        fileCopyConfig.mTargetPath
            = object.GetValue<std::string>("targetPath", common::utils::JoinPath(cDefaultTargetBaseDir, config.mType));
        fileCopyConfig.mRuntimeDir = object.GetValue<std::string>(
            "runtimeDir", common::utils::JoinPath(config.mWorkingDir, cDefaultRuntimeSubDir, config.mType));
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::sm::launcher
