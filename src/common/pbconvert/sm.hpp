/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_PBCONVERT_SM_HPP_
#define AOS_COMMON_PBCONVERT_SM_HPP_

#include <servicemanager/v5/servicemanager.grpc.pb.h>

#include <core/cm/nodeinfoprovider/itf/sminforeceiver.hpp>
#include <core/common/monitoring/itf/monitoringdata.hpp>
#include <core/common/monitoring/monitoring.hpp>
#include <core/common/types/alerts.hpp>
#include <core/common/types/common.hpp>
#include <core/common/types/envvars.hpp>
#include <core/common/types/instance.hpp>
#include <core/common/types/log.hpp>
#include <core/common/types/monitoring.hpp>
#include <core/common/types/network.hpp>
#include <core/common/types/unitconfig.hpp>

#include <servicemanager/v5/servicemanager.grpc.pb.h>

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
 * Converts grpc system log request to Aos request log.
 *
 * @param src grpc system log request.
 * @param[out] dst Aos request log.
 * @return Error
 */
Error ConvertFromProto(const servicemanager::v5::SystemLogRequest& src, RequestLog& dst);

/**
 * Converts grpc instance log request to Aos request log.
 *
 * @param src grpc instance log request.
 * @param[out] dst Aos request log.
 * @return Error
 */
Error ConvertFromProto(const servicemanager::v5::InstanceLogRequest& src, RequestLog& dst);

/**
 * Converts grpc instance crash log request to Aos request log.
 *
 * @param src grpc instance crash log request.
 * @param[out] dst Aos request log.
 * @return Error
 */
Error ConvertFromProto(const servicemanager::v5::InstanceCrashLogRequest& src, RequestLog& dst);

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
 * Converts grpc update instances to Aos instance arrays.
 *
 * @param src grpc update instances.
 * @param[out] stopInstances Aos instances to stop.
 * @param[out] startInstances Aos instances to start.
 * @return Error
 */
Error ConvertFromProto(const servicemanager::v5::UpdateInstances& src, Array<InstanceIdent>& stopInstances,
    Array<InstanceInfo>& startInstances);

/**
 * Converts grpc update networks to Aos network parameters.
 *
 * @param src grpc update networks.
 * @param[out] dst Aos network parameters array.
 * @return Error
 */
Error ConvertFromProto(const servicemanager::v5::UpdateNetworks& src, Array<NetworkParameters>& dst);

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

/**
 * Converts aos node config status to protobuf.
 *
 * @param src aos node config status.
 * @param[out] dst protobuf node config status.
 */
void ConvertToProto(const NodeConfigStatus& src, servicemanager::v5::NodeConfigStatus& dst);

/**
 * Converts aos runtime info to protobuf.
 *
 * @param src aos runtime info.
 * @param[out] dst protobuf runtime info.
 */
void ConvertToProto(const RuntimeInfo& src, servicemanager::v5::RuntimeInfo& dst);

/**
 * Converts aos resource info to protobuf.
 *
 * @param src aos resource info.
 * @param[out] dst protobuf resource info.
 */
void ConvertToProto(const ResourceInfo& src, servicemanager::v5::ResourceInfo& dst);

/**
 * Converts aos instance status to protobuf.
 *
 * @param src aos instance status.
 * @param[out] dst protobuf instance status.
 */
void ConvertToProto(const InstanceStatus& src, servicemanager::v5::InstanceStatus& dst);

/**
 * Converts aos monitoring data to protobuf.
 *
 * @param src aos monitoring data.
 * @param timestamp timestamp.
 * @param[out] dst protobuf monitoring data.
 */
void ConvertToProto(const MonitoringData& src, const Time& timestamp, servicemanager::v5::MonitoringData& dst);

/**
 * Converts aos node monitoring data to protobuf instant monitoring.
 *
 * @param src aos node monitoring data.
 * @param[out] dst protobuf instant monitoring.
 */
void ConvertToProto(const monitoring::NodeMonitoringData& src, servicemanager::v5::InstantMonitoring& dst);

/**
 * Converts aos node monitoring data to protobuf average monitoring.
 *
 * @param src aos node monitoring data.
 * @param[out] dst protobuf average monitoring.
 */
void ConvertToProto(const monitoring::NodeMonitoringData& src, servicemanager::v5::AverageMonitoring& dst);

/**
 * Converts aos push log to protobuf log data.
 *
 * @param src aos push log.
 * @param[out] dst protobuf log data.
 */
void ConvertToProto(const PushLog& src, servicemanager::v5::LogData& dst);

/**
 * Converts aos alert variant to protobuf alert.
 *
 * @param src aos alert variant.
 * @param[out] dst protobuf alert.
 */
void ConvertToProto(const AlertVariant& src, servicemanager::v5::Alert& dst);

} // namespace aos::common::pbconvert

#endif
