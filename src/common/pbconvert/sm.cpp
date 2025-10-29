/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <common/jsonprovider/jsonprovider.hpp>
#include <common/logger/logger.hpp>
#include <common/pbconvert/common.hpp>

#include "sm.hpp"

namespace aos::common::pbconvert {

namespace {

/***********************************************************************************************************************
 * Statics
 **********************************************************************************************************************/

Error ConvertFromProto(const servicemanager::v5::PartitionUsage& src, aos::PartitionUsage& dst)
{
    if (auto err = dst.mName.Assign(src.name().c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    dst.mUsedSize = static_cast<size_t>(src.used_size());

    return ErrorEnum::eNone;
}

Error ConvertFromProto(const servicemanager::v5::MonitoringData& src, aos::MonitoringData& dst)
{
    if (auto ts = pbconvert::ConvertToAos(src.timestamp()); ts.HasValue()) {
        dst.mTimestamp = ts.GetValue();
    }

    dst.mRAM = static_cast<size_t>(src.ram());
    dst.mCPU = static_cast<double>(src.cpu());

    for (const auto& part : src.partitions()) {
        if (auto err = dst.mPartitions.EmplaceBack(); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (auto err = ConvertFromProto(part, dst.mPartitions.Back()); !err.IsNone()) {
            return err;
        }
    }

    dst.mDownload = static_cast<size_t>(src.download());
    dst.mUpload   = static_cast<size_t>(src.upload());

    return ErrorEnum::eNone;
}

Error ConvertFromProto(const servicemanager::v5::InstanceMonitoring& src, aos::monitoring::InstanceMonitoringData& dst)
{
    dst.mInstanceIdent = pbconvert::ConvertToAos(src.instance());

    if (auto err = dst.mRuntimeID.Assign(src.runtime_id().c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ConvertFromProto(src.monitoring_data(), dst.mMonitoringData);
}

Error ConvertFromProto(const servicemanager::v5::SystemQuotaAlert& protoAlert,
    const google::protobuf::Timestamp& timestamp, const String& nodeID, AlertVariant& alertItem)
{
    auto alert = std::make_unique<SystemQuotaAlert>();

    alert->mTimestamp = Time::Unix(timestamp.seconds(), timestamp.nanos());
    alert->mValue     = protoAlert.value();

    if (auto err = alert->mNodeID.Assign(nodeID.CStr()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = alert->mParameter.Assign(protoAlert.parameter().c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = alert->mState.FromString(protoAlert.status().c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    alertItem.SetValue(*alert);

    return ErrorEnum::eNone;
}

Error ConvertFromProto(const servicemanager::v5::InstanceQuotaAlert& protoAlert,
    const google::protobuf::Timestamp& timestamp, AlertVariant& alertItem)
{
    auto alert = std::make_unique<InstanceQuotaAlert>();

    alert->mTimestamp                   = Time::Unix(timestamp.seconds(), timestamp.nanos());
    alert->mValue                       = protoAlert.value();
    static_cast<InstanceIdent&>(*alert) = ConvertToAos(protoAlert.instance());

    if (auto err = alert->mParameter.Assign(protoAlert.parameter().c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = alert->mState.FromString(protoAlert.status().c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    alertItem.SetValue(*alert);

    return ErrorEnum::eNone;
}

Error ConvertFromProto(const servicemanager::v5::ResourceAllocateAlert& protoAlert,
    const google::protobuf::Timestamp& timestamp, const String& nodeID, AlertVariant& alertItem)
{
    auto alert = std::make_unique<ResourceAllocateAlert>();

    alert->mTimestamp                   = Time::Unix(timestamp.seconds(), timestamp.nanos());
    static_cast<InstanceIdent&>(*alert) = ConvertToAos(protoAlert.instance());

    if (auto err = alert->mNodeID.Assign(nodeID.CStr()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = alert->mResource.Assign(protoAlert.resource().c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = alert->mMessage.Assign(protoAlert.message().c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    alertItem.SetValue(*alert);

    return ErrorEnum::eNone;
}

Error ConvertFromProto(const servicemanager::v5::SystemAlert& protoAlert, const google::protobuf::Timestamp& timestamp,
    const String& nodeID, AlertVariant& alertItem)
{
    auto alert = std::make_unique<SystemAlert>();

    alert->mTimestamp = Time::Unix(timestamp.seconds(), timestamp.nanos());

    if (auto err = alert->mNodeID.Assign(nodeID.CStr()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = alert->mMessage.Assign(protoAlert.message().c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    alertItem.SetValue(*alert);

    return ErrorEnum::eNone;
}

Error ConvertFromProto(const servicemanager::v5::CoreAlert& protoAlert, const google::protobuf::Timestamp& timestamp,
    const String& nodeID, AlertVariant& alertItem)
{
    auto alert = std::make_unique<CoreAlert>();

    alert->mTimestamp = Time::Unix(timestamp.seconds(), timestamp.nanos());

    if (auto err = alert->mNodeID.Assign(nodeID.CStr()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = alert->mCoreComponent.FromString(protoAlert.core_component().c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = alert->mMessage.Assign(protoAlert.message().c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    alertItem.SetValue(*alert);

    return ErrorEnum::eNone;
}

Error ConvertFromProto(const servicemanager::v5::InstanceAlert& protoAlert,
    const google::protobuf::Timestamp& timestamp, AlertVariant& alertItem)
{
    auto alert = std::make_unique<InstanceAlert>();

    alert->mTimestamp                   = Time::Unix(timestamp.seconds(), timestamp.nanos());
    static_cast<InstanceIdent&>(*alert) = ConvertToAos(protoAlert.instance());

    if (auto err = alert->mVersion.Assign(protoAlert.service_version().c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = alert->mMessage.Assign(protoAlert.message().c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    alertItem.SetValue(*alert);

    return ErrorEnum::eNone;
}

Error ConvertToProto(const UpdateNetworkParameters& networkParams, servicemanager::v5::UpdateNetworkParameters& result)
{
    result.set_network_id(networkParams.mNetworkID.CStr());
    result.set_subnet(networkParams.mSubnet.CStr());
    result.set_ip(networkParams.mIP.CStr());
    result.set_vlan_id(networkParams.mVlanID);

    return ErrorEnum::eNone;
}

Error ConvertFromProto(const servicemanager::v5::EnvVarStatus& grpcEnvStatus, EnvVarStatus& result)
{
    if (auto err = result.mName.Assign(grpcEnvStatus.name().c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }
    result.mError = pbconvert::ConvertFromProto(grpcEnvStatus.error());

    return ErrorEnum::eNone;
}

Error ConvertToProto(const aos::FirewallRule& src, servicemanager::v5::FirewallRule& dst)
{
    dst.set_dst_ip(src.mDstIP.CStr());
    dst.set_dst_port(src.mDstPort.CStr());
    dst.set_proto(src.mProto.CStr());
    dst.set_src_ip(src.mSrcIP.CStr());

    return ErrorEnum::eNone;
}

Error ConvertToProto(const aos::InstanceNetworkParameters& src, servicemanager::v5::NetworkParameters& dst)
{
    dst.set_network_id(src.mNetworkID.CStr());
    dst.set_subnet(src.mSubnet.CStr());
    dst.set_ip(src.mIP.CStr());

    for (const auto& dnsServer : src.mDNSServers) {
        dst.add_dns_servers(dnsServer.CStr());
    }

    for (const auto& rule : src.mFirewallRules) {
        auto* grpcRule = dst.add_rules();
        if (auto err = ConvertToProto(rule, *grpcRule); !err.IsNone()) {
            return err;
        }
    }

    return ErrorEnum::eNone;
}

Error ConvertFromProto(const servicemanager::v5::ResourceInfo& src, aos::ResourceInfo& dst)
{
    if (auto err = dst.mName.Assign(src.name().c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    dst.mSharedCount = static_cast<size_t>(src.shared_count());

    return ErrorEnum::eNone;
}

Error ConvertFromProto(const servicemanager::v5::RuntimeInfo& src, aos::RuntimeInfo& dst)
{
    if (auto err = dst.mRuntimeID.Assign(src.runtime_id().c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = dst.mRuntimeType.Assign(src.type().c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    dst.mMaxDMIPS     = src.max_dmips();
    dst.mAllowedDMIPS = src.allowed_dmips();
    dst.mTotalRAM     = src.total_ram();
    dst.mAllowedRAM   = src.allowed_ram();
    dst.mMaxInstances = static_cast<size_t>(src.max_instances());

    return ErrorEnum::eNone;
}

Error ConvertToProto(const EnvVar& src, servicemanager::v5::EnvVarInfo& dst)
{
    dst.set_name(src.mName.CStr());
    dst.set_value(src.mValue.CStr());

    return ErrorEnum::eNone;
}

Error ConvertToProto(const AlertRulePercents& src, servicemanager::v5::AlertRulePercents& dst)
{
    dst.set_min_threshold(src.mMinThreshold);
    dst.set_max_threshold(src.mMaxThreshold);

    if (src.mMinTimeout > Duration()) {
        auto* duration = dst.mutable_duration();
        duration->set_seconds(src.mMinTimeout.Seconds());
        duration->set_nanos(static_cast<int32_t>(src.mMinTimeout.Nanoseconds() % 1000000000));
    }

    return ErrorEnum::eNone;
}

Error ConvertToProto(const AlertRulePoints& src, servicemanager::v5::AlertRulePoints& dst)
{
    dst.set_min_threshold(src.mMinThreshold);
    dst.set_max_threshold(src.mMaxThreshold);

    if (src.mMinTimeout > Duration()) {
        auto* duration = dst.mutable_duration();
        duration->set_seconds(src.mMinTimeout.Seconds());
        duration->set_nanos(static_cast<int32_t>(src.mMinTimeout.Nanoseconds() % 1000000000));
    }

    return ErrorEnum::eNone;
}

Error ConvertToProto(const PartitionAlertRule& src, servicemanager::v5::PartitionAlertRule& dst)
{
    dst.set_name(src.mName.CStr());
    dst.set_min_threshold(src.mMinThreshold);
    dst.set_max_threshold(src.mMaxThreshold);

    if (src.mMinTimeout > Duration()) {
        auto* duration = dst.mutable_duration();
        duration->set_seconds(src.mMinTimeout.Seconds());
        duration->set_nanos(static_cast<int32_t>(src.mMinTimeout.Nanoseconds() % 1000000000));
    }

    return ErrorEnum::eNone;
}

Error ConvertToProto(const InstanceMonitoringParams& src, servicemanager::v5::MonitoringParameters& dst)
{
    if (src.mAlertRules.HasValue()) {
        auto* alertRules = dst.mutable_alert_rules();

        const auto& rules = src.mAlertRules.GetValue();

        if (rules.mRAM.HasValue()) {
            if (auto err = ConvertToProto(rules.mRAM.GetValue(), *alertRules->mutable_ram()); !err.IsNone()) {
                return err;
            }
        }

        if (rules.mCPU.HasValue()) {
            if (auto err = ConvertToProto(rules.mCPU.GetValue(), *alertRules->mutable_cpu()); !err.IsNone()) {
                return err;
            }
        }

        if (rules.mDownload.HasValue()) {
            if (auto err = ConvertToProto(rules.mDownload.GetValue(), *alertRules->mutable_download()); !err.IsNone()) {
                return err;
            }
        }

        if (rules.mUpload.HasValue()) {
            if (auto err = ConvertToProto(rules.mUpload.GetValue(), *alertRules->mutable_upload()); !err.IsNone()) {
                return err;
            }
        }

        for (const auto& partition : rules.mPartitions) {
            if (auto err = ConvertToProto(partition, *alertRules->add_partitions()); !err.IsNone()) {
                return err;
            }
        }
    }

    return ErrorEnum::eNone;
}

Error ConvertToProto(const aos::InstanceInfo& src, servicemanager::v5::InstanceInfo& dst)
{
    auto* instance = dst.mutable_instance();
    *instance      = pbconvert::ConvertToProto(static_cast<const InstanceIdent&>(src));

    dst.set_manifest_digest(src.mManifestDigest.CStr());
    dst.set_runtime_id(src.mRuntimeID.CStr());
    dst.set_uid(src.mUID);
    dst.set_gid(src.mGID);
    dst.set_priority(src.mPriority);
    dst.set_storage_path(src.mStoragePath.CStr());
    dst.set_state_path(src.mStatePath.CStr());

    for (const auto& envVar : src.mEnvVars) {
        auto* grpcEnvVar = dst.mutable_env_vars()->Add();

        if (auto err = ConvertToProto(envVar, *grpcEnvVar); !err.IsNone()) {
            return err;
        }
    }

    if (src.mNetworkParameters.HasValue()) {
        auto err = ConvertToProto(src.mNetworkParameters.GetValue(), *dst.mutable_network_parameters());
        if (!err.IsNone()) {
            return err;
        }
    }

    if (src.mMonitoringParams.HasValue()) {
        auto err = ConvertToProto(src.mMonitoringParams.GetValue(), *dst.mutable_monitoring_parameters());
        if (!err.IsNone()) {
            return err;
        }
    }

    return ErrorEnum::eNone;
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error ConvertFromProto(const ::common::v2::ErrorInfo& grpcError)
{
    if (grpcError.aos_code() == 0) {
        return Error(grpcError.exit_code(), grpcError.message().c_str());
    }

    ErrorEnum err = static_cast<ErrorEnum>(grpcError.aos_code());

    return Error(err, grpcError.message().c_str());
}

Error ConvertFromProto(const servicemanager::v5::NodeConfigStatus& grpcStatus, NodeConfigStatus& aosStatus)
{
    if (auto err = aosStatus.mVersion.Assign(grpcStatus.version().c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = aosStatus.mState.FromString(grpcStatus.state().c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    aosStatus.mError = pbconvert::ConvertFromProto(grpcStatus.error());

    return ErrorEnum::eNone;
}

Error ConvertToProto(const NodeConfig& config, servicemanager::v5::CheckNodeConfig& result)
{
    result.set_version(config.mVersion.CStr());

    common::jsonprovider::JSONProvider jsonProvider;
    auto nodeConfigJSON = std::make_unique<StaticString<nodeconfig::cNodeConfigJSONLen>>();

    if (auto err = jsonProvider.NodeConfigToJSON(config, *nodeConfigJSON); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    result.set_node_config(nodeConfigJSON->CStr());

    return ErrorEnum::eNone;
}

Error ConvertToProto(const NodeConfig& config, servicemanager::v5::SetNodeConfig& result)
{
    result.set_version(config.mVersion.CStr());

    common::jsonprovider::JSONProvider jsonProvider;
    auto nodeConfigJSON = std::make_unique<StaticString<nodeconfig::cNodeConfigJSONLen>>();

    if (auto err = jsonProvider.NodeConfigToJSON(config, *nodeConfigJSON); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    result.set_node_config(nodeConfigJSON->CStr());

    return ErrorEnum::eNone;
}

Error ConvertFromProto(const servicemanager::v5::Alert& grpcAlert, const String& nodeID, AlertVariant& alertItem)
{
    if (grpcAlert.has_system_quota_alert()) {
        return ConvertFromProto(grpcAlert.system_quota_alert(), grpcAlert.timestamp(), nodeID, alertItem);
    } else if (grpcAlert.has_instance_quota_alert()) {
        return ConvertFromProto(grpcAlert.instance_quota_alert(), grpcAlert.timestamp(), alertItem);
    } else if (grpcAlert.has_resource_allocate_alert()) {
        return ConvertFromProto(grpcAlert.resource_allocate_alert(), grpcAlert.timestamp(), nodeID, alertItem);
    } else if (grpcAlert.has_system_alert()) {
        return ConvertFromProto(grpcAlert.system_alert(), grpcAlert.timestamp(), nodeID, alertItem);
    } else if (grpcAlert.has_core_alert()) {
        return ConvertFromProto(grpcAlert.core_alert(), grpcAlert.timestamp(), nodeID, alertItem);
    } else if (grpcAlert.has_instance_alert()) {
        return ConvertFromProto(grpcAlert.instance_alert(), grpcAlert.timestamp(), alertItem);
    } else {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eNotSupported, "Unknown alert type"));
    }
}

Error ConvertToProto(const RequestLog& log, servicemanager::v5::SystemLogRequest& result)
{
    result.set_correlation_id(log.mCorrelationID.CStr());

    if (log.mFilter.mFrom.HasValue()) {
        *result.mutable_from() = TimestampToPB(log.mFilter.mFrom.GetValue());
    }

    if (log.mFilter.mTill.HasValue()) {
        *result.mutable_till() = TimestampToPB(log.mFilter.mTill.GetValue());
    }

    return ErrorEnum::eNone;
}

Error ConvertToProto(const RequestLog& log, servicemanager::v5::InstanceLogRequest& result)
{
    result.set_correlation_id(log.mCorrelationID.CStr());

    if (log.mFilter.mItemID.HasValue()) {
        result.mutable_filter()->set_item_id(log.mFilter.mItemID.GetValue().CStr());
    }

    if (log.mFilter.mSubjectID.HasValue()) {
        result.mutable_filter()->set_subject_id(log.mFilter.mSubjectID.GetValue().CStr());
    }

    if (log.mFilter.mInstance.HasValue()) {
        result.mutable_filter()->set_instance(log.mFilter.mInstance.GetValue());
    }

    if (log.mFilter.mFrom.HasValue()) {
        *result.mutable_from() = TimestampToPB(log.mFilter.mFrom.GetValue());
    }

    if (log.mFilter.mTill.HasValue()) {
        *result.mutable_till() = TimestampToPB(log.mFilter.mTill.GetValue());
    }

    return ErrorEnum::eNone;
}

Error ConvertToProto(const RequestLog& log, servicemanager::v5::InstanceCrashLogRequest& result)
{
    result.set_correlation_id(log.mCorrelationID.CStr());

    if (log.mFilter.mItemID.HasValue()) {
        result.mutable_filter()->set_item_id(log.mFilter.mItemID.GetValue().CStr());
    }

    if (log.mFilter.mSubjectID.HasValue()) {
        result.mutable_filter()->set_subject_id(log.mFilter.mSubjectID.GetValue().CStr());
    }

    if (log.mFilter.mInstance.HasValue()) {
        result.mutable_filter()->set_instance(log.mFilter.mInstance.GetValue());
    }

    if (log.mFilter.mFrom.HasValue()) {
        *result.mutable_from() = TimestampToPB(log.mFilter.mFrom.GetValue());
    }

    if (log.mFilter.mTill.HasValue()) {
        *result.mutable_till() = TimestampToPB(log.mFilter.mTill.GetValue());
    }

    return ErrorEnum::eNone;
}

Error ConvertFromProto(const servicemanager::v5::LogData& grpcLogData, const String& nodeID, PushLog& aosPushLog)
{
    if (auto err = aosPushLog.mCorrelationID.Assign(grpcLogData.correlation_id().c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = aosPushLog.mNodeID.Assign(nodeID.CStr()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    aosPushLog.mPartsCount = grpcLogData.part_count();
    aosPushLog.mPart       = grpcLogData.part();

    if (auto err = aosPushLog.mContent.Assign(grpcLogData.data().c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = aosPushLog.mStatus.FromString(grpcLogData.status().c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (grpcLogData.has_error()) {
        aosPushLog.mError = pbconvert::ConvertFromProto(grpcLogData.error());
    } else {
        aosPushLog.mError = ErrorEnum::eNone;
    }

    return ErrorEnum::eNone;
}

Error ConvertToProto(const Array<UpdateNetworkParameters>& networkParams, servicemanager::v5::UpdateNetworks& result)
{
    for (const auto& param : networkParams) {
        auto* network = result.add_networks();
        if (auto err = ConvertToProto(param, *network); !err.IsNone()) {
            return err;
        }
    }

    return ErrorEnum::eNone;
}

Error ConvertToProto(const Array<aos::InstanceInfo>& stopInstances, const Array<aos::InstanceInfo>& startInstances,
    servicemanager::v5::UpdateInstances& result)
{
    for (const auto& instance : stopInstances) {
        *result.add_stop_instances() = ConvertToProto(static_cast<const InstanceIdent&>(instance));
    }

    for (const auto& instance : startInstances) {
        auto* grpcInstance = result.add_start_instances();
        if (auto err = ConvertToProto(instance, *grpcInstance); !err.IsNone()) {
            return err;
        }
    }

    return ErrorEnum::eNone;
}
Error ConvertFromProto(
    const servicemanager::v5::AverageMonitoring& src, const String& nodeID, aos::monitoring::NodeMonitoringData& dst)
{
    // Set node ID
    if (auto err = dst.mNodeID.Assign(nodeID.CStr()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    // Set timestamp from node_monitoring data
    if (auto ts = pbconvert::ConvertToAos(src.node_monitoring().timestamp()); ts.HasValue()) {
        dst.mTimestamp = ts.GetValue();
    }

    // Convert node monitoring data
    if (auto err = ConvertFromProto(src.node_monitoring(), dst.mMonitoringData); !err.IsNone()) {
        return err;
    }

    // Convert instances monitoring data
    for (const auto& inst : src.instances_monitoring()) {
        if (auto err = dst.mInstances.EmplaceBack(); !err.IsNone()) {
            return err;
        }

        if (auto err = ConvertFromProto(inst, dst.mInstances.Back()); !err.IsNone()) {
            return err;
        }
    }

    return ErrorEnum::eNone;
}

Error ConvertFromProto(const servicemanager::v5::InstanceStatus& src, const String& nodeID, aos::InstanceStatus& dst)
{
    static_cast<InstanceIdent&>(dst) = pbconvert::ConvertToAos(src.instance());

    if (auto err = dst.mVersion.Assign(src.version().c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = dst.mNodeID.Assign(nodeID.CStr()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = dst.mRuntimeID.Assign(src.runtime_id().c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    for (auto protoEnvVarStatus : src.env_vars()) {
        if (auto err = dst.mEnvVarsStatuses.EmplaceBack(); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (auto err = ConvertFromProto(protoEnvVarStatus, dst.mEnvVarsStatuses.Back()); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    if (auto err = dst.mState.FromString(src.state().c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    dst.mError = pbconvert::ConvertFromProto(src.error());

    return ErrorEnum::eNone;
}

Error ConvertFromProto(
    const servicemanager::v5::InstantMonitoring& src, const String& nodeID, aos::monitoring::NodeMonitoringData& dst)
{
    // Set node ID
    if (auto err = dst.mNodeID.Assign(nodeID.CStr()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    // Set timestamp from node_monitoring data
    if (auto ts = pbconvert::ConvertToAos(src.node_monitoring().timestamp()); ts.HasValue()) {
        dst.mTimestamp = ts.GetValue();
    }

    // Convert node monitoring data
    if (auto err = ConvertFromProto(src.node_monitoring(), dst.mMonitoringData); !err.IsNone()) {
        return err;
    }

    // Convert instances monitoring data
    for (const auto& inst : src.instances_monitoring()) {
        if (auto err = dst.mInstances.EmplaceBack(); !err.IsNone()) {
            return err;
        }

        if (auto err = ConvertFromProto(inst, dst.mInstances.Back()); !err.IsNone()) {
            return err;
        }
    }

    return ErrorEnum::eNone;
}

Error ConvertFromProto(const servicemanager::v5::SMInfo& src, aos::cm::nodeinfoprovider::SMInfo& dst)
{
    if (auto err = dst.mNodeID.Assign(src.node_id().c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    for (const auto& grpcResource : src.resources()) {
        if (auto err = dst.mResources.EmplaceBack(); !err.IsNone()) {
            return err;
        }

        if (auto err = ConvertFromProto(grpcResource, dst.mResources.Back()); !err.IsNone()) {
            return err;
        }
    }

    for (const auto& grpcRuntime : src.runtimes()) {
        if (auto err = dst.mRuntimes.EmplaceBack(); !err.IsNone()) {
            return err;
        }

        if (auto err = ConvertFromProto(grpcRuntime, dst.mRuntimes.Back()); !err.IsNone()) {
            return err;
        }
    }

    return ErrorEnum::eNone;
}

} // namespace aos::common::pbconvert
