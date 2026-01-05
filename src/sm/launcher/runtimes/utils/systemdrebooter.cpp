/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <core/common/tools/logger.hpp>

#include "systemdrebooter.hpp"

namespace aos::sm::launcher::utils {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error SystemdRebooter::Init(aos::sm::utils::SystemdConnItf& systemdConn)
{
    LOG_DBG() << "Initialize systemd rebooter";

    mSystemdConn = &systemdConn;

    return ErrorEnum::eNone;
}

Error SystemdRebooter::Reboot()
{
    LOG_DBG() << "System reboot requested";

    if (auto err = mSystemdConn->StartUnit(cRebootTarget, cReplaceMode, cTimeout); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::sm::launcher::utils
