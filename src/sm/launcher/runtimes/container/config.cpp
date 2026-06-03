/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <common/utils/exception.hpp>
#include <common/utils/filesystem.hpp>

#include "config.hpp"

namespace aos::sm::launcher {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

void ParseContainerConfig(
    const common::utils::CaseInsensitiveObjectWrapper& object, const std::string& workingDir, ContainerConfig& config)
{
    config.mRuntimeDir = object.GetValue<std::string>("runtimeDir", "/run/aos/runtime");
    config.mHostWhiteoutsDir
        = object.GetValue<std::string>("hostWhiteoutsDir", common::utils::JoinPath(workingDir, "whiteouts"));
    config.mStorageDir = object.GetValue<std::string>("storageDir", common::utils::JoinPath(workingDir, "storages"));
    config.mStateDir   = object.GetValue<std::string>("stateDir", common::utils::JoinPath(workingDir, "states"));
    config.mHostBinds  = common::utils::GetArrayValue<std::string>(object, "hostBinds");
}

} // namespace aos::sm::launcher
