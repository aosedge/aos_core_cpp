/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PBCONVERT_SM_HPP_
#define PBCONVERT_SM_HPP_

#include <aos/common/cloudprotocol/alerts.hpp>
#include <aos/common/cloudprotocol/envvars.hpp>
#include <aos/common/cloudprotocol/log.hpp>
#include <aos/common/monitoring/monitoring.hpp>
#include <aos/common/types.hpp>
#include <aos/sm/networkmanager.hpp>

#include <servicemanager/v4/servicemanager.grpc.pb.h>

namespace aos::common::pbconvert {

/**
 * Converts aos push log to protobuf.
 *
 * @param src push log to convert.
 * @return ::servicemanager::v4::LogData.
 */
::servicemanager::v4::LogData ConvertToProto(const cloudprotocol::PushLog& src);

/**
 * Converts aos monitoring data to protobuf.
 *
 * @param src monitoring data to convert.
 * @param timestamp monitoring data timestamp.
 * @return ::servicemanager::v4::MonitoringData.
 */
::servicemanager::v4::MonitoringData ConvertToProto(const monitoring::MonitoringData& src, const Time& timestamp);

/**
 * Converts aos node monitoring data to protobuf avarage monitoring.
 *
 * @param src node monitoring data to convert.
 * @return ::servicemanager::v4::AverageMonitoring.
 */
::servicemanager::v4::AverageMonitoring ConvertToProtoAvarageMonitoring(const monitoring::NodeMonitoringData& src);

/**
 * Converts aos node monitoring data to protobuf instant monitoring.
 *
 * @param src node monitoring data to convert.
 * @return ::servicemanager::v4::InstantMonitoring.
 */
::servicemanager::v4::InstantMonitoring ConvertToProtoInstantMonitoring(const monitoring::NodeMonitoringData& src);

/**
 * Converts aos instance status to protobuf.
 *
 * @param src instance status to convert.
 * @return ::servicemanager::v4::InstanceStatus.
 */
::servicemanager::v4::InstanceStatus ConvertToProto(const InstanceStatus& src);

/**
 * Converts aos instance filter to protobuf.
 *
 * @param src aos instance filter.
 * @return ::servicemanager::v4::InstanceFilter.
 */
::servicemanager::v4::InstanceFilter ConvertToProto(const cloudprotocol::InstanceFilter& src);

/**
 * Converts aos env var status to protobuf.
 *
 * @param src aos env var status.
 * @return ::servicemanager::v4::EnvVarStatus.
 */
::servicemanager::v4::EnvVarStatus ConvertToProto(const cloudprotocol::EnvVarStatus& src);

/**
 * Converts aos alerts to protobuf.
 *
 * @param src aos alert.
 * @return ::servicemanager::v4::Alert.
 */
::servicemanager::v4::Alert ConvertToProto(const cloudprotocol::AlertVariant& src);

/**
 * Converts protobuf network parameters to aos.
 *
 * @param val protobuf network parameters.
 * @return NetworkParameters.
 */
NetworkParameters ConvertToAos(const ::servicemanager::v4::NetworkParameters& val);

/**
 * Converts protobuf instance info to aos.
 *
 * @param val protobuf instance info.
 * @return InstanceInfo.
 */
InstanceInfo ConvertToAos(const ::servicemanager::v4::InstanceInfo& val);

/**
 * Converts protobuf instance filter to aos.
 *
 * @param val protobuf instance filter.
 * @return cloudprotocol::InstanceFilter.
 */
cloudprotocol::InstanceFilter ConvertToAos(const ::servicemanager::v4::InstanceFilter& val);

/**
 * Converts protobuf env var info to aos.
 *
 * @param val protobuf env var info.
 * @return cloudprotocol::EnvVarInfo.
 */
cloudprotocol::EnvVarInfo ConvertToAos(const ::servicemanager::v4::EnvVarInfo& val);

/**
 * Converts protobuf env vars instance info to aos.
 *
 * @param src protobuf env vars instance info array.
 * @param dst[out] aos env vars instance info array.
 * @return Error.
 */
Error ConvertToAos(const ::servicemanager::v4::OverrideEnvVars& src, cloudprotocol::EnvVarsInstanceInfoArray& dst);

/**
 * Converts service info to aos.
 *
 * @param val protobuf service info.
 * @return ServiceInfo.
 */
ServiceInfo ConvertToAos(const ::servicemanager::v4::ServiceInfo& val);

/**
 * Converts layer info to aos.
 *
 * @param val protobuf layer info.
 * @return LayerInfo.
 */
LayerInfo ConvertToAos(const ::servicemanager::v4::LayerInfo& val);

/**
 * Converts system log request to aos.
 *
 * @param val protobuf system log request.
 * @return cloudprotocol::RequestLog.
 */
cloudprotocol::RequestLog ConvertToAos(const ::servicemanager::v4::SystemLogRequest& val);

/**
 * Converts instance log request to aos.
 *
 * @param val protobuf instance log request.
 * @return cloudprotocol::RequestLog.
 */
cloudprotocol::RequestLog ConvertToAos(const ::servicemanager::v4::InstanceLogRequest& val);

/**
 * Converts instance crash log request to aos.
 *
 * @param val protobuf instance crash log request.
 * @return cloudprotocol::RequestLog.
 */
cloudprotocol::RequestLog ConvertToAos(const ::servicemanager::v4::InstanceCrashLogRequest& val);

} // namespace aos::common::pbconvert

#endif
