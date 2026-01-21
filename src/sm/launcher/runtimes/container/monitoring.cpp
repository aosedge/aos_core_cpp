/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <core/common/tools/logger.hpp>

#include "monitoring.hpp"

namespace aos::sm::launcher {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error Monitoring::StartInstanceMonitoring(const std::string& instanceID)
{
    LOG_DBG() << "Start instance monitoring" << Log::Field("instanceID", instanceID.c_str());

    return ErrorEnum::eNone;
}

Error Monitoring::StopInstanceMonitoring(const std::string& instanceID)
{
    LOG_DBG() << "Stop instance monitoring" << Log::Field("instanceID", instanceID.c_str());

    return ErrorEnum::eNone;
}

Error Monitoring::GetInstanceMonitoringData(
    const std::string& instanceID, monitoring::InstanceMonitoringData& monitoringData)
{
    (void)monitoringData;

    LOG_DBG() << "Get instance monitoring data" << Log::Field("instanceID", instanceID.c_str());

    return ErrorEnum::eNotSupported;
}

}; // namespace aos::sm::launcher
