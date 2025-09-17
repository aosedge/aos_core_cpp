/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "sm.hpp"
#include "common.hpp"

namespace {

google::protobuf::RepeatedPtrField<::servicemanager::v4::InstanceMonitoring> ConvertInstancesMonitoring(
    const aos::monitoring::NodeMonitoringData& src)
{
    google::protobuf::RepeatedPtrField<::servicemanager::v4::InstanceMonitoring> result;

    for (const auto& instance : src.mServiceInstances) {
        auto& instanceMonitoring = *result.Add();

        *instanceMonitoring.mutable_instance() = aos::common::pbconvert::ConvertToProto(instance.mInstanceIdent);
        *instanceMonitoring.mutable_monitoring_data()
            = aos::common::pbconvert::ConvertToProto(instance.mMonitoringData, src.mTimestamp);
    }

    return result;
}

class AlertVisitor : public aos::StaticVisitor<::servicemanager::v4::Alert> {
public:
    Res Visit(const aos::cloudprotocol::SystemAlert& val) const
    {
        Res   result  = CreateAlert(val);
        auto& pbAlert = *result.mutable_system_alert();

        pbAlert.set_message(val.mMessage.CStr());

        return result;
    }

    Res Visit(const aos::cloudprotocol::CoreAlert& val) const
    {
        Res   result  = CreateAlert(val);
        auto& pbAlert = *result.mutable_core_alert();

        pbAlert.set_core_component(val.mCoreComponent.ToString().CStr());
        pbAlert.set_message(val.mMessage.CStr());

        return result;
    }

    Res Visit(const aos::cloudprotocol::SystemQuotaAlert& val) const
    {
        Res   result  = CreateAlert(val);
        auto& pbAlert = *result.mutable_system_quota_alert();

        pbAlert.set_parameter(val.mParameter.CStr());
        pbAlert.set_value(val.mValue);
        pbAlert.set_status(val.mStatus.ToString().CStr());

        return result;
    }

    Res Visit(const aos::cloudprotocol::InstanceQuotaAlert& val) const
    {
        Res   result  = CreateAlert(val);
        auto& pbAlert = *result.mutable_instance_quota_alert();

        *pbAlert.mutable_instance() = aos::common::pbconvert::ConvertToProto(val.mInstanceIdent);
        pbAlert.set_parameter(val.mParameter.CStr());
        pbAlert.set_value(val.mValue);
        pbAlert.set_status(val.mStatus.ToString().CStr());

        return result;
    }

    Res Visit(const aos::cloudprotocol::DeviceAllocateAlert& val) const
    {
        Res   result  = CreateAlert(val);
        auto& pbAlert = *result.mutable_device_allocate_alert();

        *pbAlert.mutable_instance() = aos::common::pbconvert::ConvertToProto(val.mInstanceIdent);
        pbAlert.set_device(val.mDevice.CStr());
        pbAlert.set_message(val.mMessage.CStr());

        return result;
    }

    Res Visit(const aos::cloudprotocol::ResourceValidateAlert& val) const
    {
        Res   result  = CreateAlert(val);
        auto& pbAlert = *result.mutable_resource_validate_alert();

        pbAlert.set_name(val.mName.CStr());

        for (const auto& error : val.mErrors) {
            *pbAlert.add_errors() = aos::common::pbconvert::ConvertAosErrorToProto(error);
        }

        return result;
    }

    Res Visit(const aos::cloudprotocol::DownloadAlert& val) const { return CreateAlert(val); }

    Res Visit(const aos::cloudprotocol::ServiceInstanceAlert& val) const
    {
        Res   result  = CreateAlert(val);
        auto& pbAlert = *result.mutable_instance_alert();

        *pbAlert.mutable_instance() = aos::common::pbconvert::ConvertToProto(val.mInstanceIdent);
        pbAlert.set_service_version(val.mServiceVersion.CStr());
        pbAlert.set_message(val.mMessage.CStr());

        return result;
    }

private:
    Res CreateAlert(const aos::cloudprotocol::AlertItem& src) const
    {
        Res pbAlert;

        pbAlert.set_tag(src.mTag.ToString().CStr());
        *pbAlert.mutable_timestamp() = aos::common::pbconvert::TimestampToPB(src.mTimestamp);

        return pbAlert;
    }
};

} // namespace

namespace aos::common::pbconvert {

::servicemanager::v4::LogData ConvertToProto(const cloudprotocol::PushLog& src)
{
    ::servicemanager::v4::LogData result;

    result.set_log_id(src.mLogID.CStr());
    result.set_part_count(src.mPartsCount);
    result.set_part(src.mPart);
    result.set_data(std::string(src.mContent.CStr(), src.mContent.Size()));
    result.set_status(src.mStatus.ToString().CStr());

    SetErrorInfo(src.mErrorInfo, result);

    return result;
}

::servicemanager::v4::MonitoringData ConvertToProto(const monitoring::MonitoringData& src, const Time& timestamp)
{
    ::servicemanager::v4::MonitoringData result;

    result.set_ram(src.mRAM);
    result.set_cpu(static_cast<uint64_t>(src.mCPU));
    result.set_download(src.mDownload);
    result.set_upload(src.mUpload);
    result.mutable_timestamp()->CopyFrom(TimestampToPB(timestamp));

    for (const auto& partition : src.mPartitions) {
        auto& pbPartition = *result.add_partitions();

        pbPartition.set_name(partition.mName.CStr());
        pbPartition.set_used_size(partition.mUsedSize);
    }

    return result;
}

::servicemanager::v4::AverageMonitoring ConvertToProtoAvarageMonitoring(const monitoring::NodeMonitoringData& src)
{
    ::servicemanager::v4::AverageMonitoring result;

    *result.mutable_node_monitoring()      = ConvertToProto(src.mMonitoringData, src.mTimestamp);
    *result.mutable_instances_monitoring() = ConvertInstancesMonitoring(src);

    return result;
}

::servicemanager::v4::InstantMonitoring ConvertToProtoInstantMonitoring(const monitoring::NodeMonitoringData& src)
{
    ::servicemanager::v4::InstantMonitoring result;

    *result.mutable_node_monitoring()      = ConvertToProto(src.mMonitoringData, src.mTimestamp);
    *result.mutable_instances_monitoring() = ConvertInstancesMonitoring(src);

    return result;
}

::servicemanager::v4::InstanceStatus ConvertToProto(const InstanceStatus& src)
{
    ::servicemanager::v4::InstanceStatus result;

    *result.mutable_instance() = ConvertToProto(src.mInstanceIdent);
    result.set_service_version(src.mVersion.CStr());
    result.set_run_state(src.mState.ToString().CStr());
    SetErrorInfo(src.mError, result);

    return result;
}

::servicemanager::v4::InstanceFilter ConvertToProto(const cloudprotocol::InstanceFilter& src)
{
    ::servicemanager::v4::InstanceFilter result;

    if (src.mServiceID.HasValue()) {
        result.set_service_id(src.mServiceID.GetValue().CStr());
    }

    if (src.mSubjectID.HasValue()) {
        result.set_subject_id(src.mSubjectID.GetValue().CStr());
    }

    result.set_instance(-1);
    if (src.mInstance.HasValue()) {
        result.set_instance(static_cast<int64_t>(src.mInstance.GetValue()));
    }

    return result;
}

::servicemanager::v4::EnvVarStatus ConvertToProto(const cloudprotocol::EnvVarStatus& src)
{
    ::servicemanager::v4::EnvVarStatus result;

    result.set_name(src.mName.CStr());

    SetErrorInfo(src.mError, result);

    return result;
}

::servicemanager::v4::Alert ConvertToProto(const cloudprotocol::AlertVariant& src)
{
    AlertVisitor visitor;

    return src.ApplyVisitor(visitor);
}

Error ConvertToAos(const ::servicemanager::v4::NetworkParameters& val, NetworkParameters& dst)
{
    dst.mNetworkID = String(val.network_id().c_str());
    dst.mSubnet    = String(val.subnet().c_str());
    dst.mIP        = String(val.ip().c_str());
    dst.mVlanID    = val.vlan_id();

    for (const auto& dns : val.dns_servers()) {
        if (auto err = dst.mDNSServers.PushBack(String(dns.c_str())); !err.IsNone()) {
            return AOS_ERROR_WRAP(
                Error(err, "received network parameters dns servers count exceeds application limit"));
        }
    }

    for (const auto& rule : val.rules()) {
        FirewallRule firewallRule;

        firewallRule.mDstIP   = String(rule.dst_ip().c_str());
        firewallRule.mDstPort = String(rule.dst_port().c_str());
        firewallRule.mProto   = String(rule.proto().c_str());
        firewallRule.mSrcIP   = String(rule.src_ip().c_str());

        if (auto err = dst.mFirewallRules.PushBack(Move(firewallRule)); !err.IsNone()) {
            return AOS_ERROR_WRAP(Error(err, "received network parameters rules count exceeds application limit"));
        }
    }

    return ErrorEnum::eNone;
}

Error ConvertToAos(const ::servicemanager::v4::InstanceInfo& val, InstanceInfoObsolete& dst)
{
    dst.mInstanceIdent = ConvertToAos(val.instance());
    dst.mUID           = val.uid();
    dst.mPriority      = val.priority();
    dst.mStoragePath   = String(val.storage_path().c_str());
    dst.mStatePath     = String(val.state_path().c_str());

    if (auto err = ConvertToAos(val.network_parameters(), dst.mNetworkParameters); !err.IsNone()) {
        return err;
    }

    return ErrorEnum::eNone;
}

Error ConvertToAos(const ::servicemanager::v4::InstanceFilter& val, cloudprotocol::InstanceFilter& dst)
{
    if (const auto& serviceID = val.service_id(); !serviceID.empty()) {
        dst.mServiceID.SetValue(String(serviceID.c_str()));
    }

    if (const auto& subjectID = val.subject_id(); !subjectID.empty()) {
        dst.mSubjectID.SetValue(String(subjectID.c_str()));
    }

    if (const auto instanceID = val.instance(); instanceID != -1) {
        dst.mInstance.SetValue(static_cast<uint64_t>(instanceID));
    }

    return ErrorEnum::eNone;
}

Error ConvertToAos(const ::servicemanager::v4::EnvVarInfo& val, cloudprotocol::EnvVarInfo& dst)
{
    dst.mName  = String(val.name().c_str());
    dst.mValue = String(val.value().c_str());
    dst.mTTL   = ConvertToAos(val.ttl());

    return ErrorEnum::eNone;
}

Error ConvertToAos(const ::servicemanager::v4::OverrideEnvVars& src, cloudprotocol::EnvVarsInstanceInfoArray& dst)
{
    for (const auto& envVar : src.env_vars()) {
        cloudprotocol::InstanceFilter instanceFilter;

        if (auto err = ConvertToAos(envVar.instance_filter(), instanceFilter); !err.IsNone()) {
            return err;
        }

        auto variables = std::make_unique<cloudprotocol::EnvVarInfoArray>();

        for (const auto& var : envVar.variables()) {
            cloudprotocol::EnvVarInfo envVarInfo;

            if (auto err = ConvertToAos(var, envVarInfo); !err.IsNone()) {
                return err;
            }

            if (auto err = variables->PushBack(envVarInfo); !err.IsNone()) {
                return AOS_ERROR_WRAP(Error(err, "received instance's env vars count exceeds application limit"));
            }
        }

        if (auto err = dst.EmplaceBack(instanceFilter, *variables); !err.IsNone()) {
            return AOS_ERROR_WRAP(Error(err, "received env vars instances count exceeds application limit"));
        }
    }

    return ErrorEnum::eNone;
}

Error ConvertToAos(const ::servicemanager::v4::ServiceInfo& val, ServiceInfo& dst)
{
    dst.mServiceID  = String(val.service_id().c_str());
    dst.mProviderID = String(val.provider_id().c_str());
    dst.mVersion    = String(val.version().c_str());
    dst.mGID        = val.gid();
    dst.mURL        = String(val.url().c_str());
    dst.mSHA256     = {reinterpret_cast<const uint8_t*>(val.sha256().c_str()), val.sha256().length()};
    dst.mSize       = val.size();

    return ErrorEnum::eNone;
}

Error ConvertToAos(const ::servicemanager::v4::LayerInfo& val, LayerInfo& dst)
{
    dst.mLayerID     = String(val.layer_id().c_str());
    dst.mLayerDigest = String(val.digest().c_str());
    dst.mVersion     = String(val.version().c_str());
    dst.mURL         = String(val.url().c_str());
    dst.mSHA256      = {reinterpret_cast<const uint8_t*>(val.sha256().c_str()), val.sha256().length()};
    dst.mSize        = val.size();

    return ErrorEnum::eNone;
}

Error ConvertToAos(const ::servicemanager::v4::SystemLogRequest& val, cloudprotocol::RequestLog& dst)
{
    dst.mLogID        = String(val.log_id().c_str());
    dst.mFilter.mFrom = ConvertToAos(val.from());
    dst.mFilter.mTill = ConvertToAos(val.till());

    return ErrorEnum::eNone;
}

Error ConvertToAos(const ::servicemanager::v4::InstanceLogRequest& val, cloudprotocol::RequestLog& dst)
{
    dst.mLogID        = String(val.log_id().c_str());
    dst.mFilter.mFrom = ConvertToAos(val.from());
    dst.mFilter.mTill = ConvertToAos(val.till());

    if (auto err = ConvertToAos(val.instance_filter(), dst.mFilter.mInstanceFilter); !err.IsNone()) {
        return err;
    }

    return ErrorEnum::eNone;
}

Error ConvertToAos(const ::servicemanager::v4::InstanceCrashLogRequest& val, cloudprotocol::RequestLog& dst)
{
    dst.mLogID        = String(val.log_id().c_str());
    dst.mFilter.mFrom = ConvertToAos(val.from());
    dst.mFilter.mTill = ConvertToAos(val.till());

    if (auto err = ConvertToAos(val.instance_filter(), dst.mFilter.mInstanceFilter); !err.IsNone()) {
        return err;
    }

    return ErrorEnum::eNone;
}

} // namespace aos::common::pbconvert
