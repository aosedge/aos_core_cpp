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

    if (src.max_dmips() > 0) {
        dst.mMaxDMIPS.SetValue(src.max_dmips());
    }

    if (src.allowed_dmips() > 0) {
        dst.mAllowedDMIPS.SetValue(src.allowed_dmips());
    }

    if (src.total_ram() > 0) {
        dst.mTotalRAM.SetValue(src.total_ram());
    }

    if (src.allowed_ram() > 0) {
        dst.mAllowedRAM.SetValue(src.allowed_ram());
    }

    dst.mMaxInstances = static_cast<size_t>(src.max_instances());

    if (auto err = ConvertToAos(src.os_info(), dst.mOSInfo); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = ConvertToAos(src.arch_info(), dst.mArchInfo); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

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
    dst.set_owner_id(src.mOwnerID.CStr());
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

Error ConvertFromProto(const servicemanager::v5::FirewallRule& src, FirewallRule& dst)
{
    if (auto err = dst.mDstIP.Assign(src.dst_ip().c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = dst.mDstPort.Assign(src.dst_port().c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = dst.mProto.Assign(src.proto().c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = dst.mSrcIP.Assign(src.src_ip().c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error ConvertFromProto(const servicemanager::v5::NetworkParameters& src, InstanceNetworkParameters& dst)
{
    if (auto err = dst.mNetworkID.Assign(src.network_id().c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = dst.mSubnet.Assign(src.subnet().c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = dst.mIP.Assign(src.ip().c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    for (const auto& dns : src.dns_servers()) {
        if (auto err = dst.mDNSServers.EmplaceBack(); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (auto err = dst.mDNSServers.Back().Assign(dns.c_str()); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    for (const auto& rule : src.rules()) {
        if (auto err = dst.mFirewallRules.EmplaceBack(); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (auto err = ConvertFromProto(rule, dst.mFirewallRules.Back()); !err.IsNone()) {
            return err;
        }
    }

    return ErrorEnum::eNone;
}

Error ConvertFromProto(const servicemanager::v5::InstanceInfo& src, InstanceInfo& dst)
{
    static_cast<InstanceIdent&>(dst) = ConvertToAos(src.instance());

    if (auto err = dst.mManifestDigest.Assign(src.manifest_digest().c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = dst.mOwnerID.Assign(src.owner_id().c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = dst.mRuntimeID.Assign(src.runtime_id().c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    dst.mUID      = src.uid();
    dst.mGID      = src.gid();
    dst.mPriority = src.priority();

    if (auto err = dst.mStoragePath.Assign(src.storage_path().c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = dst.mStatePath.Assign(src.state_path().c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    for (const auto& envVar : src.env_vars()) {
        if (auto err = dst.mEnvVars.EmplaceBack(); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (auto err = dst.mEnvVars.Back().mName.Assign(envVar.name().c_str()); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (auto err = dst.mEnvVars.Back().mValue.Assign(envVar.value().c_str()); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    if (src.has_network_parameters()) {
        dst.mNetworkParameters.EmplaceValue();

        if (auto err = ConvertFromProto(src.network_parameters(), *dst.mNetworkParameters); !err.IsNone()) {
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

void ConvertToProto(const NodeConfigStatus& src, servicemanager::v5::NodeConfigStatus& dst)
{
    dst.set_state(src.mState.ToString().CStr());
    dst.set_version(src.mVersion.CStr());
    dst.mutable_error()->CopyFrom(ConvertAosErrorToProto(src.mError));
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

Error ConvertFromProto(const servicemanager::v5::SystemLogRequest& src, RequestLog& dst)
{
    dst.mLogType = LogTypeEnum::eSystemLog;

    if (auto err = dst.mCorrelationID.Assign(src.correlation_id().c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (src.has_from()) {
        dst.mFilter.mFrom.EmplaceValue(pbconvert::ConvertToAos(src.from()).GetValue());
    }

    if (src.has_till()) {
        dst.mFilter.mTill.EmplaceValue(pbconvert::ConvertToAos(src.till()).GetValue());
    }

    return ErrorEnum::eNone;
}

Error ConvertFromProto(const servicemanager::v5::InstanceLogRequest& src, RequestLog& dst)
{
    dst.mLogType = LogTypeEnum::eInstanceLog;

    if (auto err = dst.mCorrelationID.Assign(src.correlation_id().c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (src.has_filter()) {
        pbconvert::ConvertToAos(src.filter(), dst.mFilter);
    }

    if (src.has_from()) {
        dst.mFilter.mFrom.EmplaceValue(pbconvert::ConvertToAos(src.from()).GetValue());
    }

    if (src.has_till()) {
        dst.mFilter.mTill.EmplaceValue(pbconvert::ConvertToAos(src.till()).GetValue());
    }

    return ErrorEnum::eNone;
}

Error ConvertFromProto(const servicemanager::v5::InstanceCrashLogRequest& src, RequestLog& dst)
{
    dst.mLogType = LogTypeEnum::eCrashLog;

    if (auto err = dst.mCorrelationID.Assign(src.correlation_id().c_str()); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (src.has_filter()) {
        pbconvert::ConvertToAos(src.filter(), dst.mFilter);
    }

    if (src.has_from()) {
        dst.mFilter.mFrom.EmplaceValue(pbconvert::ConvertToAos(src.from()).GetValue());
    }

    if (src.has_till()) {
        dst.mFilter.mTill.EmplaceValue(pbconvert::ConvertToAos(src.till()).GetValue());
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

Error ConvertFromProto(const servicemanager::v5::UpdateInstances& src, Array<InstanceIdent>& stopInstances,
    Array<InstanceInfo>& startInstances)
{
    for (const auto& instance : src.stop_instances()) {
        if (auto err = stopInstances.EmplaceBack(ConvertToAos(instance)); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    for (const auto& instance : src.start_instances()) {
        if (auto err = startInstances.EmplaceBack(); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (auto err = ConvertFromProto(instance, startInstances.Back()); !err.IsNone()) {
            return err;
        }
    }

    return ErrorEnum::eNone;
}

Error ConvertFromProto(const servicemanager::v5::UpdateNetworks& src, Array<NetworkParameters>& dst)
{
    for (const auto& network : src.networks()) {
        if (auto err = dst.EmplaceBack(); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        auto& dstNetwork = dst.Back();

        if (auto err = dstNetwork.mNetworkID.Assign(network.network_id().c_str()); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (auto err = dstNetwork.mSubnet.Assign(network.subnet().c_str()); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (auto err = dstNetwork.mIP.Assign(network.ip().c_str()); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        dstNetwork.mVlanID = network.vlan_id();
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

void ConvertToProto(const RuntimeInfo& src, servicemanager::v5::RuntimeInfo& dst)
{
    dst.set_runtime_id(src.mRuntimeID.CStr());
    dst.set_type(src.mRuntimeType.CStr());

    if (src.mMaxDMIPS.HasValue()) {
        dst.set_max_dmips(*src.mMaxDMIPS);
    }

    if (src.mAllowedDMIPS.HasValue()) {
        dst.set_allowed_dmips(*src.mAllowedDMIPS);
    }

    if (src.mTotalRAM.HasValue()) {
        dst.set_total_ram(*src.mTotalRAM);
    }

    if (src.mAllowedRAM.HasValue()) {
        dst.set_allowed_ram(*src.mAllowedRAM);
    }

    dst.set_max_instances(src.mMaxInstances);

    ConvertToProto(src.mOSInfo, *dst.mutable_os_info());
    ConvertToProto(src.mArchInfo, *dst.mutable_arch_info());
}

void ConvertToProto(const ResourceInfo& src, servicemanager::v5::ResourceInfo& dst)
{
    dst.set_name(src.mName.CStr());
    dst.set_shared_count(src.mSharedCount);
}

void ConvertToProto(const InstanceStatus& src, servicemanager::v5::InstanceStatus& dst)
{
    dst.mutable_instance()->CopyFrom(ConvertToProto(static_cast<const InstanceIdent&>(src)));
    dst.set_version(src.mVersion.CStr());
    dst.set_preinstalled(src.mPreinstalled);
    dst.set_runtime_id(src.mRuntimeID.CStr());
    dst.set_manifest_digest(src.mManifestDigest.CStr());

    for (const auto& envVarStatus : src.mEnvVarsStatuses) {
        auto* protoEnvVarStatus = dst.add_env_vars();
        protoEnvVarStatus->set_name(envVarStatus.mName.CStr());
        protoEnvVarStatus->mutable_error()->CopyFrom(ConvertAosErrorToProto(envVarStatus.mError));
    }

    dst.set_state(src.mState.ToString().CStr());
    dst.mutable_error()->CopyFrom(ConvertAosErrorToProto(src.mError));
}

void ConvertToProto(const MonitoringData& src, const Time& timestamp, servicemanager::v5::MonitoringData& dst)
{
    dst.mutable_timestamp()->CopyFrom(TimestampToPB(timestamp));
    dst.set_ram(src.mRAM);
    dst.set_cpu(src.mCPU);
    dst.set_download(src.mDownload);
    dst.set_upload(src.mUpload);

    for (const auto& partition : src.mPartitions) {
        auto* protoPartition = dst.add_partitions();
        protoPartition->set_name(partition.mName.CStr());
        protoPartition->set_used_size(partition.mUsedSize);
    }
}

void ConvertToProto(const monitoring::NodeMonitoringData& src, servicemanager::v5::InstantMonitoring& dst)
{
    ConvertToProto(src.mMonitoringData, src.mTimestamp, *dst.mutable_node_monitoring());

    for (const auto& instance : src.mInstances) {
        auto* instanceMonitoring = dst.add_instances_monitoring();
        instanceMonitoring->mutable_instance()->CopyFrom(ConvertToProto(instance.mInstanceIdent));
        instanceMonitoring->set_runtime_id(instance.mRuntimeID.CStr());
        ConvertToProto(instance.mMonitoringData, src.mTimestamp, *instanceMonitoring->mutable_monitoring_data());
    }
}

void ConvertToProto(const monitoring::NodeMonitoringData& src, servicemanager::v5::AverageMonitoring& dst)
{
    ConvertToProto(src.mMonitoringData, src.mTimestamp, *dst.mutable_node_monitoring());

    for (const auto& instance : src.mInstances) {
        auto* instanceMonitoring = dst.add_instances_monitoring();
        instanceMonitoring->mutable_instance()->CopyFrom(ConvertToProto(instance.mInstanceIdent));
        instanceMonitoring->set_runtime_id(instance.mRuntimeID.CStr());
        ConvertToProto(instance.mMonitoringData, src.mTimestamp, *instanceMonitoring->mutable_monitoring_data());
    }
}

void ConvertToProto(const PushLog& src, servicemanager::v5::LogData& dst)
{
    dst.set_correlation_id(src.mCorrelationID.CStr());
    dst.set_part_count(src.mPartsCount);
    dst.set_part(src.mPart);
    dst.set_data(src.mContent.CStr());
    dst.set_status(src.mStatus.ToString().CStr());
    dst.mutable_error()->CopyFrom(ConvertAosErrorToProto(src.mError));
}

/***********************************************************************************************************************
 * Alert conversion
 **********************************************************************************************************************/

class AlertVisitor : public aos::StaticVisitor<void> {
public:
    explicit AlertVisitor(servicemanager::v5::Alert& alert)
        : mAlert(alert)
    {
    }

    void Visit(const aos::SystemAlert& val) const
    {
        mAlert.mutable_timestamp()->CopyFrom(TimestampToPB(val.mTimestamp));
        mAlert.mutable_system_alert()->set_message(val.mMessage.CStr());
    }

    void Visit(const aos::CoreAlert& val) const
    {
        mAlert.mutable_timestamp()->CopyFrom(TimestampToPB(val.mTimestamp));

        auto* coreAlert = mAlert.mutable_core_alert();

        coreAlert->set_core_component(val.mCoreComponent.ToString().CStr());
        coreAlert->set_message(val.mMessage.CStr());
    }

    void Visit(const aos::SystemQuotaAlert& val) const
    {
        mAlert.mutable_timestamp()->CopyFrom(TimestampToPB(val.mTimestamp));

        auto* quotaAlert = mAlert.mutable_system_quota_alert();

        quotaAlert->set_parameter(val.mParameter.CStr());
        quotaAlert->set_value(val.mValue);
        quotaAlert->set_status(val.mState.ToString().CStr());
    }

    void Visit(const aos::InstanceQuotaAlert& val) const
    {
        mAlert.mutable_timestamp()->CopyFrom(TimestampToPB(val.mTimestamp));

        auto* quotaAlert = mAlert.mutable_instance_quota_alert();

        quotaAlert->mutable_instance()->CopyFrom(ConvertToProto(static_cast<const aos::InstanceIdent&>(val)));
        quotaAlert->set_parameter(val.mParameter.CStr());
        quotaAlert->set_value(val.mValue);
        quotaAlert->set_status(val.mState.ToString().CStr());
    }

    void Visit(const aos::ResourceAllocateAlert& val) const
    {
        mAlert.mutable_timestamp()->CopyFrom(TimestampToPB(val.mTimestamp));

        auto* resourceAlert = mAlert.mutable_resource_allocate_alert();

        resourceAlert->mutable_instance()->CopyFrom(ConvertToProto(static_cast<const aos::InstanceIdent&>(val)));
        resourceAlert->set_resource(val.mResource.CStr());
        resourceAlert->set_message(val.mMessage.CStr());
    }

    void Visit([[maybe_unused]] const aos::DownloadAlert& val) const { }

    void Visit(const aos::InstanceAlert& val) const
    {
        mAlert.mutable_timestamp()->CopyFrom(TimestampToPB(val.mTimestamp));

        auto* instanceAlert = mAlert.mutable_instance_alert();

        instanceAlert->mutable_instance()->CopyFrom(ConvertToProto(static_cast<const aos::InstanceIdent&>(val)));
        instanceAlert->set_service_version(val.mVersion.CStr());
        instanceAlert->set_message(val.mMessage.CStr());
    }

private:
    servicemanager::v5::Alert& mAlert;
};

void ConvertToProto(const AlertVariant& src, servicemanager::v5::Alert& dst)
{
    AlertVisitor visitor(dst);

    src.ApplyVisitor(visitor);
}

} // namespace aos::common::pbconvert
