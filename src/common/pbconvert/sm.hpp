/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_PBCONVERT_SM_HPP_
#define AOS_COMMON_PBCONVERT_SM_HPP_

#include <core/common/monitoring/monitoring.hpp>
#include <core/common/types/alerts.hpp>
#include <core/common/types/envvars.hpp>
#include <core/common/types/log.hpp>
#include <core/common/types/obsolete.hpp>
#include <core/sm/networkmanager/networkmanager.hpp>

#include <servicemanager/v4/servicemanager.grpc.pb.h>

namespace aos::common::pbconvert {

/**
 * Converts Aos push log to protobuf.
 *
 * @param src push log to convert.
 * @return ::servicemanager::v4::LogData.
 */
::servicemanager::v4::LogData ConvertToProto(const PushLog& src);

/**
 * Converts Aos monitoring data to protobuf.
 *
 * @param src monitoring data to convert.
 * @param timestamp monitoring data timestamp.
 * @return ::servicemanager::v4::MonitoringData.
 */
::servicemanager::v4::MonitoringData ConvertToProto(const monitoring::MonitoringData& src, const Time& timestamp);

/**
 * Converts Aos node monitoring data to protobuf avarage monitoring.
 *
 * @param src node monitoring data to convert.
 * @return ::servicemanager::v4::AverageMonitoring.
 */
::servicemanager::v4::AverageMonitoring ConvertToProtoAvarageMonitoring(const monitoring::NodeMonitoringData& src);

/**
 * Converts Aos node monitoring data to protobuf instant monitoring.
 *
 * @param src node monitoring data to convert.
 * @return ::servicemanager::v4::InstantMonitoring.
 */
::servicemanager::v4::InstantMonitoring ConvertToProtoInstantMonitoring(const monitoring::NodeMonitoringData& src);

/**
 * Converts Aos instance status to protobuf.
 *
 * @param src instance status to convert.
 * @return ::servicemanager::v4::InstanceStatus.
 */
::servicemanager::v4::InstanceStatus ConvertToProto(const InstanceStatus& src);

/**
 * Converts Aos instance filter to protobuf.
 *
 * @param src Aos instance filter.
 * @return ::servicemanager::v4::InstanceFilter.
 */
::servicemanager::v4::InstanceFilter ConvertToProto(const InstanceFilter& src);

/**
 * Converts Aos env var status to protobuf.
 *
 * @param src Aos env var status.
 * @return ::servicemanager::v4::EnvVarStatus.
 */
::servicemanager::v4::EnvVarStatus ConvertToProto(const EnvVarStatus& src);

/**
 * Converts Aos alerts to protobuf.
 *
 * @param src Aos alert.
 * @return ::servicemanager::v4::Alert.
 */
::servicemanager::v4::Alert ConvertToProto(const AlertVariant& src);

/**
 * Converts protobuf network parameters to Aos.
 *
 * @param val protobuf network parameters.
 * @param dst[out] Aos network parameters.
 * @return Error.
 */
Error ConvertToAos(const ::servicemanager::v4::NetworkParameters& val, NetworkParameters& dst);

/**
 * Converts protobuf instance info to Aos.
 *
 * @param val protobuf instance info.
 * @param dst[out] Aos instance info.
 * @return Error.
 */
Error ConvertToAos(const ::servicemanager::v4::InstanceInfo& val, InstanceInfo& dst);

/**
 * Converts protobuf instance filter to Aos.
 *
 * @param val protobuf instance filter.
 * @param dst[out] Aos instance filter.
 * @return Error.
 */
Error ConvertToAos(const ::servicemanager::v4::InstanceFilter& val, InstanceFilter& dst);

/**
 * Converts protobuf env var info to Aos.
 *
 * @param val protobuf env var info.
 * @param dst[out] Aos env var info.
 * @return Error.
 */
Error ConvertToAos(const ::servicemanager::v4::EnvVarInfo& val, EnvVarInfo& dst);

/**
 * Converts protobuf env vars instance info to Aos.
 *
 * @param src protobuf env vars instance info array.
 * @param dst[out] Aos env vars instance info array.
 * @return Error.
 */
Error ConvertToAos(const ::servicemanager::v4::OverrideEnvVars& src, EnvVarsInstanceInfoArray& dst);

/**
 * Converts service info to Aos.
 *
 * @param val protobuf service info.
 * @param dst[out] Aos service info.
 * @return Error.
 */
Error ConvertToAos(const ::servicemanager::v4::ServiceInfo& val, ServiceInfo& dst);

/**
 * Converts layer info to Aos.
 *
 * @param val protobuf layer info.
 * @param dst[out] Aos layer info.
 * @return Error.
 */
Error ConvertToAos(const ::servicemanager::v4::LayerInfo& val, LayerInfo& dst);

/**
 * Converts system log request to Aos.
 *
 * @param val protobuf system log request.
 * @param dst[out] Aos log request.
 * @return Error.
 */
Error ConvertToAos(const ::servicemanager::v4::SystemLogRequest& val, RequestLog& dst);

/**
 * Converts instance log request to Aos.
 *
 * @param val protobuf instance log request.
 * @param dst[out] Aos log request.
 * @return Error.
 */
Error ConvertToAos(const ::servicemanager::v4::InstanceLogRequest& val, RequestLog& dst);

/**
 * Converts instance crash log request to Aos.
 *
 * @param val protobuf instance crash log request.
 * @param dst[out] Aos log request.
 * @return Error.
 */
Error ConvertToAos(const ::servicemanager::v4::InstanceCrashLogRequest& val, RequestLog& dst);

} // namespace aos::common::pbconvert

#endif
