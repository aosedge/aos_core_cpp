/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef RESOURCEUSAGEPROVIDER_HPP_
#define RESOURCEUSAGEPROVIDER_HPP_

#include <aos/common/monitoring/monitoring.hpp>

namespace aos::sm::monitoring {

/**
 * Resource usage provider interface.
 */
class ResourceUsageProvider : public aos::monitoring::ResourceUsageProviderItf {
public:
    /**
     * Returns node monitoring data.
     *
     * @param nodeID node ident.
     * @param[out] monitoringData monitoring data.
     * @return Error.
     */
    Error GetNodeMonitoringData(const String& nodeID, aos::monitoring::MonitoringData& monitoringData) override
    {
        (void)nodeID;
        (void)monitoringData;

        return ErrorEnum::eNone;
    }

    /**
     * Returns instance monitoring data.
     *
     * @param instanceID instance ID.
     * @param[out] monitoringData monitoring data.
     * @return Error.
     */
    Error GetInstanceMonitoringData(
        const String& instanceID, aos::monitoring::InstanceMonitoringData& monitoringData) override
    {
        (void)instanceID;
        (void)monitoringData;

        return ErrorEnum::eNone;
    }
};

} // namespace aos::sm::monitoring

#endif
