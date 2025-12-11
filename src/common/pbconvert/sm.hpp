/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_PBCONVERT_SM_HPP_
#define AOS_COMMON_PBCONVERT_SM_HPP_

#include <servicemanager/v5/servicemanager.grpc.pb.h>

#include <core/cm/nodeinfoprovider/itf/sminforeceiver.hpp>
#include <core/common/monitoring/monitoring.hpp>
#include <core/common/types/alerts.hpp>
#include <core/common/types/envvars.hpp>
#include <core/common/types/instance.hpp>
#include <core/common/types/log.hpp>
#include <core/common/types/monitoring.hpp>
#include <core/common/types/network.hpp>
#include <core/common/types/unitconfig.hpp>

namespace aos::common::pbconvert {

/**
 * Converts ErrorInfo from grpc to Aos.
 *
 * @param grpcError grpc error info.
 * @return Error.
 */
Error ConvertFromProto(const ::common::v2::ErrorInfo& grpcError);

/**
 * Converts NodeConfigStatus from grpc to Aos.
 *
 * @param grpcStatus grpc node config status.
 * @param aosStatus Aos node config status.
 */
Error ConvertFromProto(const servicemanager::v5::NodeConfigStatus& grpcStatus, NodeConfigStatus& aosStatus);

/**
 * Converts Aos node config to grpc check node config message.
 *
 * @param config node config.
 * @param result check node config message.
 * @return Error
 */
Error ConvertToProto(const NodeConfig& config, servicemanager::v5::CheckNodeConfig& result);

/**
 * Converts Aos node config to grpc set node config message.
 *
 * @param config node config.
 * @param result check node config message.
 * @return Error
 */
Error ConvertToProto(const NodeConfig& config, servicemanager::v5::SetNodeConfig& result);

/**
 * Converts grpc alert to Aos alert item.
 *
 * @param grpcAlert grpc alert.
 * @param nodeID node ID.
 * @param alertItem Aos alert item.
 * @return Error
 */
Error ConvertFromProto(const servicemanager::v5::Alert& grpcAlert, const String& nodeID, AlertVariant& alertItem);

/**
 * Converts Aos request log to grpc system log request.
 *
 * @param log Aos request log.
 * @param result grpc system log request.
 * @return Error
 */
Error ConvertToProto(const RequestLog& log, servicemanager::v5::SystemLogRequest& result);

/**
 * Converts Aos request log to grpc instance log request.
 *
 * @param log Aos request log.
 * @param result grpc instance log request.
 * @return Error
 */
Error ConvertToProto(const RequestLog& log, servicemanager::v5::InstanceLogRequest& result);

/**
 * Converts Aos request log to grpc instance crash log request.
 *
 * @param log Aos request log.
 * @param result grpc instance crash log request.
 * @return Error
 */
Error ConvertToProto(const RequestLog& log, servicemanager::v5::InstanceCrashLogRequest& result);

/**
 * Converts grpc log data to Aos push log.
 *
 * @param grpcLogData grpc log data.
 * @param nodeID node ID.
 * @param aosPushLog Aos push log.
 * @return Error
 */
Error ConvertFromProto(const servicemanager::v5::LogData& grpcLogData, const String& nodeID, PushLog& aosPushLog);

/**
 * Converts Aos array of update network parameters to grpc update networks message.
 *
 * @param networkParams Aos array of update network parameters.
 * @param result grpc update networks message.
 * @return Error
 */
Error ConvertToProto(const Array<UpdateNetworkParameters>& networkParams, servicemanager::v5::UpdateNetworks& result);

/**
 * Converts Aos instance info arrays to grpc update instances message.
 *
 * @param stopInstances Aos instances to stop.
 * @param startInstances Aos instances to start.
 * @param result grpc update instances message.
 * @return Error
 */
Error ConvertToProto(const Array<aos::InstanceInfo>& stopInstances, const Array<aos::InstanceInfo>& startInstances,
    servicemanager::v5::UpdateInstances& result);

/**
 * Converts grpc average monitoring to Aos node monitoring data.
 *
 * @param src grpc average monitoring.
 * @param nodeID node ID.
 * @param dst Aos node monitoring data.
 * @return Error
 */
Error ConvertFromProto(
    const servicemanager::v5::AverageMonitoring& src, const String& nodeID, aos::monitoring::NodeMonitoringData& dst);

/**
 * Converts grpc instance status to Aos instance status.
 *
 * @param src grpc instance status.
 * @param nodeID node ID to set in the result.
 * @param dst Aos instance status.
 * @return Error
 */
Error ConvertFromProto(const servicemanager::v5::InstanceStatus& src, const String& nodeID, aos::InstanceStatus& dst);

/**
 * Converts grpc instant monitoring to Aos node monitoring data.
 *
 * @param src grpc instant monitoring.
 * @param nodeID node ID to set in the result.
 * @param dst Aos node monitoring data.
 * @return Error
 */
Error ConvertFromProto(
    const servicemanager::v5::InstantMonitoring& src, const String& nodeID, aos::monitoring::NodeMonitoringData& dst);

/**
 * Converts grpc SM info to Aos SM info.
 *
 * @param src grpc SM info.
 * @param dst Aos SM info.
 * @return Error
 */
Error ConvertFromProto(const servicemanager::v5::SMInfo& src, aos::cm::nodeinfoprovider::SMInfo& dst);

} // namespace aos::common::pbconvert

#endif
