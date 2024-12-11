/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gmock/gmock.h>

#include <aos/test/log.hpp>

#include "pbconvert/sm.hpp"

using namespace testing;

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

namespace {

void CompareTimestamps(const aos::Time& lhs, const google::protobuf::Timestamp& rhs)
{
    EXPECT_EQ(lhs.UnixTime().tv_sec, rhs.seconds());
    EXPECT_EQ(lhs.UnixTime().tv_nsec, rhs.nanos());
}

aos::PartitionInfo CreatePartition(const aos::String& name, size_t usedSize)
{
    aos::PartitionInfo result;

    result.mName     = name;
    result.mUsedSize = usedSize;

    return result;
}

} // namespace
class PBConvertSMTest : public Test {
public:
    void SetUp() override { aos::InitLog(); }
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(PBConvertSMTest, ConvertPushLogToProto)
{
    aos::cloudprotocol::PushLog param;

    param.mLogID      = "log-id";
    param.mPartsCount = 2;
    param.mPart       = 2;
    param.mContent    = "content";
    param.mStatus     = aos::cloudprotocol::LogStatusEnum::eOk;
    param.mErrorInfo  = aos::ErrorEnum::eNone;

    ::servicemanager::v4::LogData result = aos::common::pbconvert::ConvertToProto(param);

    EXPECT_EQ(aos::String(result.log_id().c_str()), param.mLogID);
    EXPECT_EQ(result.part_count(), param.mPartsCount);
    EXPECT_EQ(result.part(), param.mPart);
    EXPECT_EQ(aos::String(result.data().c_str()), param.mContent);
    EXPECT_EQ(aos::String(result.status().c_str()), param.mStatus.ToString());
    EXPECT_FALSE(result.has_error());
}

TEST_F(PBConvertSMTest, ConvertMonitoringDataToProto)
{
    aos::monitoring::MonitoringData param;
    aos::Time                       timestamp = aos::Time::Now();

    param.mRAM      = 1;
    param.mCPU      = 2;
    param.mDownload = 3;
    param.mUpload   = 4;

    param.mPartitions.PushBack(CreatePartition("partition1", 10));

    ::servicemanager::v4::MonitoringData result = aos::common::pbconvert::ConvertToProto(param, timestamp);

    EXPECT_EQ(result.ram(), param.mRAM);
    EXPECT_EQ(result.cpu(), param.mCPU);
    EXPECT_EQ(result.download(), param.mDownload);
    EXPECT_EQ(result.upload(), param.mUpload);

    EXPECT_EQ(result.timestamp().seconds(), timestamp.UnixTime().tv_sec);

    ASSERT_EQ(result.partitions_size(), param.mPartitions.Size());

    for (size_t i = 0; i < param.mPartitions.Size(); ++i) {
        const auto& partition   = param.mPartitions[i];
        const auto& pbPartition = result.partitions(i);

        EXPECT_EQ(pbPartition.name(), partition.mName.CStr());
        EXPECT_EQ(pbPartition.used_size(), partition.mUsedSize);
    }
}

TEST_F(PBConvertSMTest, ConvertNodeMonitoringDataToAvarageMonitoring)
{
    aos::monitoring::NodeMonitoringData param;

    param.mTimestamp = aos::Time::Now();

    aos::InstanceIdent              instanceIdent {"service-id", "subject-id", 1};
    aos::monitoring::MonitoringData monitoringData;

    monitoringData.mCPU = 1000;

    param.mServiceInstances.PushBack({instanceIdent, monitoringData});
    param.mMonitoringData.mCPU = 2000;

    ::servicemanager::v4::AverageMonitoring result = aos::common::pbconvert::ConvertToProtoAvarageMonitoring(param);

    EXPECT_EQ(result.node_monitoring().cpu(), param.mMonitoringData.mCPU);
    CompareTimestamps(param.mTimestamp, result.node_monitoring().timestamp());

    ASSERT_EQ(result.instances_monitoring_size(), param.mServiceInstances.Size());
    for (size_t i = 0; i < param.mServiceInstances.Size(); ++i) {
        const auto& instanceMonitoring   = param.mServiceInstances[i];
        const auto& pbInstanceMonitoring = result.instances_monitoring(i);

        EXPECT_EQ(aos::String(pbInstanceMonitoring.instance().service_id().c_str()), instanceIdent.mServiceID);
        EXPECT_EQ(aos::String(pbInstanceMonitoring.instance().subject_id().c_str()), instanceIdent.mSubjectID);
        EXPECT_EQ(pbInstanceMonitoring.instance().instance(), instanceIdent.mInstance);

        EXPECT_EQ(pbInstanceMonitoring.monitoring_data().cpu(), instanceMonitoring.mMonitoringData.mCPU);
        CompareTimestamps(param.mTimestamp, pbInstanceMonitoring.monitoring_data().timestamp());
    }
}

TEST_F(PBConvertSMTest, ConvertNodeMonitoringDataToInstantMonitoring)
{
    aos::monitoring::NodeMonitoringData param;

    param.mTimestamp = aos::Time::Now();

    aos::InstanceIdent              instanceIdent {"service-id", "subject-id", 1};
    aos::monitoring::MonitoringData monitoringData;

    monitoringData.mCPU = 1000;

    param.mServiceInstances.PushBack({instanceIdent, monitoringData});
    param.mMonitoringData.mCPU = 2000;

    ::servicemanager::v4::InstantMonitoring result = aos::common::pbconvert::ConvertToProtoInstantMonitoring(param);

    EXPECT_EQ(result.node_monitoring().cpu(), param.mMonitoringData.mCPU);
    CompareTimestamps(param.mTimestamp, result.node_monitoring().timestamp());

    ASSERT_EQ(result.instances_monitoring_size(), param.mServiceInstances.Size());
    for (size_t i = 0; i < param.mServiceInstances.Size(); ++i) {
        const auto& instanceMonitoring   = param.mServiceInstances[i];
        const auto& pbInstanceMonitoring = result.instances_monitoring(i);

        EXPECT_EQ(aos::String(pbInstanceMonitoring.instance().service_id().c_str()), instanceIdent.mServiceID);
        EXPECT_EQ(aos::String(pbInstanceMonitoring.instance().subject_id().c_str()), instanceIdent.mSubjectID);
        EXPECT_EQ(pbInstanceMonitoring.instance().instance(), instanceIdent.mInstance);

        EXPECT_EQ(pbInstanceMonitoring.monitoring_data().cpu(), instanceMonitoring.mMonitoringData.mCPU);
        CompareTimestamps(param.mTimestamp, pbInstanceMonitoring.monitoring_data().timestamp());
    }
}

TEST_F(PBConvertSMTest, ConvertInstanceStatusToProto)
{
    aos::InstanceStatus param;

    param.mInstanceIdent  = aos::InstanceIdent {"service-id", "subject-id", 1};
    param.mServiceVersion = "1.0.0";
    param.mRunState       = aos::InstanceRunStateEnum::eActive;

    ::servicemanager::v4::InstanceStatus result = aos::common::pbconvert::ConvertToProto(param);

    EXPECT_EQ(aos::String(result.instance().service_id().c_str()), param.mInstanceIdent.mServiceID);
    EXPECT_EQ(aos::String(result.instance().subject_id().c_str()), param.mInstanceIdent.mSubjectID);
    EXPECT_EQ(result.instance().instance(), param.mInstanceIdent.mInstance);

    EXPECT_EQ(aos::String(result.service_version().c_str()), param.mServiceVersion);
    EXPECT_EQ(aos::String(result.run_state().c_str()), param.mRunState.ToString());
}

TEST_F(PBConvertSMTest, ConvertInstanceFilterToProto)
{
    aos::Optional<aos::StaticString<aos::cServiceIDLen>> serviceIDNullopt {};
    aos::Optional<aos::StaticString<aos::cSubjectIDLen>> subjectIDNullopt {};
    aos::Optional<uint64_t>                              instanceNullopt {};

    aos::cloudprotocol::InstanceFilter params[] = {
        {serviceIDNullopt, subjectIDNullopt, instanceNullopt},
        {serviceIDNullopt, {"subject-id"}, {1}},
        {{"service-id"}, subjectIDNullopt, {1}},
        {{"service-id"}, {"subject-id"}, instanceNullopt},
        {{"service-id"}, {"subject-id"}, {1}},
    };

    size_t iteration = 0;

    for (const auto& param : params) {
        LOG_INF() << "Test iteration: " << iteration++;

        ::servicemanager::v4::InstanceFilter result = aos::common::pbconvert::ConvertToProto(param);

        if (param.mServiceID.HasValue()) {
            EXPECT_EQ(result.service_id(), param.mServiceID.GetValue().CStr());
        } else {
            EXPECT_TRUE(result.service_id().empty());
        }

        if (param.mSubjectID.HasValue()) {
            EXPECT_EQ(result.subject_id(), param.mSubjectID.GetValue().CStr());
        } else {
            EXPECT_TRUE(result.subject_id().empty());
        }

        if (param.mInstance.HasValue()) {
            EXPECT_EQ(result.instance(), param.mInstance.GetValue());
        } else {
            EXPECT_EQ(result.instance(), -1);
        }
    }
}

TEST_F(PBConvertSMTest, ConvertEnvVarStatusToProto)
{
    aos::cloudprotocol::EnvVarStatus params[] = {
        {"name1", aos::Error {aos::ErrorEnum::eFailed, "failed error"}},
        {"name2", aos::Error {aos::ErrorEnum::eRuntime, "runtime error"}},
        {"name3", aos::Error {aos::ErrorEnum::eNone}},
    };

    size_t iteration = 0;

    for (const auto& param : params) {
        LOG_INF() << "Test iteration: " << iteration++;

        ::servicemanager::v4::EnvVarStatus result = aos::common::pbconvert::ConvertToProto(param);

        EXPECT_EQ(aos::String(result.name().c_str()), param.mName);

        if (param.mError.IsNone()) {
            EXPECT_FALSE(result.has_error());
        } else {
            EXPECT_TRUE(result.has_error());

            EXPECT_EQ(result.error().aos_code(), static_cast<int32_t>(param.mError.Value()));
            EXPECT_EQ(result.error().exit_code(), param.mError.Errno());
            EXPECT_EQ(aos::String(result.error().message().c_str()), param.mError.Message());
        }
    }
}

TEST_F(PBConvertSMTest, ConvertNetworkParametersToAos)
{
    ::servicemanager::v4::NetworkParameters param;

    param.set_network_id("network-id");
    param.set_subnet("subnet");
    param.set_ip("ip");
    param.set_vlan_id(1);

    for (const auto& dns : {"dns1", "dns2"}) {
        param.add_dns_servers(dns);
    }

    for (const auto& ruleSfx : {"1", "2"}) {
        auto& rule = *param.add_rules();

        rule.set_dst_ip(std::string("dst-ip").append(ruleSfx));
        rule.set_dst_port(std::string("40").append(ruleSfx));
        rule.set_proto(std::string("proto").append(ruleSfx));
        rule.set_src_ip(std::string("src-ip").append(ruleSfx));
    }

    auto result = aos::common::pbconvert::ConvertToAos(param);

    EXPECT_EQ(result.mNetworkID, aos::String(param.network_id().c_str()));
    EXPECT_EQ(result.mSubnet, aos::String(param.subnet().c_str()));
    EXPECT_EQ(result.mIP, aos::String(param.ip().c_str()));
    EXPECT_EQ(result.mVlanID, param.vlan_id());

    ASSERT_EQ(result.mDNSServers.Size(), param.dns_servers_size());
    for (size_t i = 0; i < result.mDNSServers.Size(); ++i) {
        EXPECT_EQ(result.mDNSServers[i], aos::String(param.dns_servers(i).c_str()));
    }

    ASSERT_EQ(result.mFirewallRules.Size(), param.rules_size());
    for (size_t i = 0; i < result.mFirewallRules.Size(); ++i) {
        const auto& rule   = result.mFirewallRules[i];
        const auto& pbRule = param.rules(i);

        EXPECT_EQ(rule.mDstIP, aos::String(pbRule.dst_ip().c_str()));
        EXPECT_EQ(rule.mDstPort, aos::String(pbRule.dst_port().c_str()));
        EXPECT_EQ(rule.mProto, aos::String(pbRule.proto().c_str()));
        EXPECT_EQ(rule.mSrcIP, aos::String(pbRule.src_ip().c_str()));
    }
}

TEST_F(PBConvertSMTest, ConvertInstanceInfoToAos)
{
    ::servicemanager::v4::InstanceInfo param;

    param.mutable_instance()->set_service_id("service-id");
    param.mutable_instance()->set_subject_id("subject-id");
    param.mutable_instance()->set_instance(1);

    param.set_uid(10);
    param.set_storage_path("storage-path");
    param.set_state_path("state-path");

    param.mutable_network_parameters()->set_network_id("network-id");

    auto result = aos::common::pbconvert::ConvertToAos(param);

    EXPECT_EQ(result.mInstanceIdent.mServiceID, aos::String(param.instance().service_id().c_str()));
    EXPECT_EQ(result.mInstanceIdent.mSubjectID, aos::String(param.instance().subject_id().c_str()));
    EXPECT_EQ(result.mInstanceIdent.mInstance, param.instance().instance());

    EXPECT_EQ(result.mUID, param.uid());
    EXPECT_EQ(result.mStoragePath, aos::String(param.storage_path().c_str()));
    EXPECT_EQ(result.mStatePath, aos::String(param.state_path().c_str()));

    EXPECT_EQ(result.mNetworkParameters.mNetworkID, aos::String(param.network_parameters().network_id().c_str()));
}

TEST_F(PBConvertSMTest, ConvertInstanceFilterToAos)
{
    struct {
        std::string serviceID;
        std::string subjectID;
        int64_t     instance;
    } params[] = {
        {"service-id", "subject-id", 1},
        {"service-id", "subject-id", -1},
        {"service-id", "", 1},
        {"", "subject-id", 1},
        {"", "", -1},
    };

    size_t iteration = 0;

    for (const auto& param : params) {
        LOG_INF() << "Test iteration: " << iteration++;

        ::servicemanager::v4::InstanceFilter pbParam;

        pbParam.set_service_id(param.serviceID);
        pbParam.set_subject_id(param.subjectID);
        pbParam.set_instance(param.instance);

        auto result = aos::common::pbconvert::ConvertToAos(pbParam);

        if (!param.serviceID.empty()) {
            EXPECT_EQ(result.mServiceID.GetValue(), aos::String(param.serviceID.c_str()));
        } else {
            EXPECT_FALSE(result.mServiceID.HasValue());
        }

        if (!param.subjectID.empty()) {
            EXPECT_EQ(result.mSubjectID.GetValue(), aos::String(param.subjectID.c_str()));
        } else {
            EXPECT_FALSE(result.mSubjectID.HasValue());
        }

        if (param.instance != -1) {
            EXPECT_EQ(result.mInstance.GetValue(), param.instance);
        } else {
            EXPECT_FALSE(result.mInstance.HasValue());
        }
    }
}

TEST_F(PBConvertSMTest, ConvertEnvVarInfoToAos)
{
    ::servicemanager::v4::EnvVarInfo param;

    param.set_name("name");
    param.set_value("value");
    param.mutable_ttl()->set_seconds(1);

    auto result = aos::common::pbconvert::ConvertToAos(param);

    EXPECT_EQ(result.mName, aos::String(param.name().c_str()));
    EXPECT_EQ(result.mValue, aos::String(param.value().c_str()));
    EXPECT_EQ(result.mTTL, aos::Time::Unix(1, 0));
}

TEST_F(PBConvertSMTest, ConvertOverrideEnvVarsToAosSucceeds)
{
    ::servicemanager::v4::OverrideEnvVars param;

    auto& instanceEnvVar = *param.add_env_vars();
    instanceEnvVar.mutable_instance_filter()->set_service_id("service-id");
    instanceEnvVar.mutable_instance_filter()->set_instance(-1);

    auto& instanceEnvVariables = *instanceEnvVar.mutable_variables()->Add();
    instanceEnvVariables.set_name("name");
    instanceEnvVariables.set_value("value");
    instanceEnvVariables.mutable_ttl()->set_seconds(1);

    aos::cloudprotocol::EnvVarsInstanceInfoArray result;

    auto err = aos::common::pbconvert::ConvertToAos(param, result);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    ASSERT_EQ(result.Size(), 1);

    EXPECT_EQ(result[0].mFilter.mServiceID.GetValue(), aos::String("service-id"));
    EXPECT_FALSE(result[0].mFilter.mInstance.HasValue());
    EXPECT_FALSE(result[0].mFilter.mSubjectID.HasValue());

    ASSERT_EQ(result[0].mVariables.Size(), 1);

    EXPECT_EQ(result[0].mVariables[0].mName, aos::String("name"));
    EXPECT_EQ(result[0].mVariables[0].mValue, aos::String("value"));
    EXPECT_EQ(result[0].mVariables[0].mTTL, aos::Time::Unix(1, 0));
}

TEST_F(PBConvertSMTest, ConvertOverrideEnvVarsToAosReturnsErrorOnInstanceEnvVarLimitExceeded)
{
    ::servicemanager::v4::OverrideEnvVars param;

    auto& instanceEnvVar = *param.add_env_vars();
    instanceEnvVar.mutable_instance_filter()->set_service_id("service-id");
    instanceEnvVar.mutable_instance_filter()->set_instance(-1);

    for (size_t i = 0; i < aos::cMaxNumEnvVariables + 1; ++i) {
        auto& instanceEnvVariables = *instanceEnvVar.mutable_variables()->Add();
        instanceEnvVariables.set_name("name");
        instanceEnvVariables.set_value("value");
        instanceEnvVariables.mutable_ttl()->set_seconds(1);
    }

    aos::cloudprotocol::EnvVarsInstanceInfoArray result;

    auto err = aos::common::pbconvert::ConvertToAos(param, result);
    ASSERT_TRUE(err.Is(aos::ErrorEnum::eNoMemory)) << err.Message();
}

TEST_F(PBConvertSMTest, ConvertOverrideEnvVarsToAosReturnsErrorOnInstancesLimitExceeded)
{
    ::servicemanager::v4::OverrideEnvVars param;

    for (size_t i = 0; i < aos::cMaxNumInstances + 1; ++i) {
        auto& instanceEnvVar = *param.add_env_vars();
        instanceEnvVar.mutable_instance_filter()->set_service_id("service-id");
        instanceEnvVar.mutable_instance_filter()->set_instance(-1);

        auto& instanceEnvVariables = *instanceEnvVar.mutable_variables()->Add();
        instanceEnvVariables.set_name("name");
        instanceEnvVariables.set_value("value");
        instanceEnvVariables.mutable_ttl()->set_seconds(1);
    }

    aos::cloudprotocol::EnvVarsInstanceInfoArray result;

    auto err = aos::common::pbconvert::ConvertToAos(param, result);
    ASSERT_TRUE(err.Is(aos::ErrorEnum::eNoMemory)) << err.Message();
}

TEST_F(PBConvertSMTest, ConvertServiceInfoToAos)
{
    ::servicemanager::v4::ServiceInfo param;

    param.set_service_id("service-id");
    param.set_provider_id("provider-id");
    param.set_version("1.0.0");
    param.set_gid(10);
    param.set_url("url");
    param.set_sha256("sha256");
    param.set_size(100);

    auto result = aos::common::pbconvert::ConvertToAos(param);

    EXPECT_EQ(result.mServiceID, aos::String(param.service_id().c_str()));
    EXPECT_EQ(result.mProviderID, aos::String(param.provider_id().c_str()));
    EXPECT_EQ(result.mVersion, aos::String(param.version().c_str()));
    EXPECT_EQ(result.mGID, param.gid());
    EXPECT_EQ(result.mURL, aos::String(param.url().c_str()));
    EXPECT_EQ(result.mSHA256, aos::String(param.sha256().c_str()));
    EXPECT_EQ(result.mSize, param.size());
}

TEST_F(PBConvertSMTest, ConvertLayerInfoToAos)
{
    ::servicemanager::v4::LayerInfo param;

    param.set_layer_id("layer-id");
    param.set_digest("digest");
    param.set_version("1.0.0");
    param.set_url("url");
    param.set_sha256("sha256");
    param.set_size(100);

    auto result = aos::common::pbconvert::ConvertToAos(param);

    EXPECT_EQ(result.mLayerID, aos::String(param.layer_id().c_str()));
    EXPECT_EQ(result.mLayerDigest, aos::String(param.digest().c_str()));
    EXPECT_EQ(result.mVersion, aos::String(param.version().c_str()));
    EXPECT_EQ(result.mURL, aos::String(param.url().c_str()));
    EXPECT_EQ(result.mSHA256, aos::String(param.sha256().c_str()));
    EXPECT_EQ(result.mSize, param.size());
}

TEST_F(PBConvertSMTest, ConvertSystemLogRequestToAos)
{
    ::servicemanager::v4::SystemLogRequest param;

    param.set_log_id("log-id");
    param.mutable_from()->set_seconds(100);
    param.mutable_till()->set_seconds(200);

    auto result = aos::common::pbconvert::ConvertToAos(param);

    EXPECT_EQ(result.mLogID, aos::String(param.log_id().c_str()));
    EXPECT_EQ(result.mFilter.mFrom, aos::Time::Unix(100, 0));
    EXPECT_EQ(result.mFilter.mTill, aos::Time::Unix(200, 0));
}

TEST_F(PBConvertSMTest, ConvertInstanceLogRequestToAos)
{
    ::servicemanager::v4::InstanceLogRequest param;

    aos::cloudprotocol::InstanceFilter instanceFilter;
    instanceFilter.mServiceID.SetValue("service-id");
    instanceFilter.mInstance.SetValue(1);

    param.set_log_id("log-id");
    param.mutable_from()->set_seconds(100);
    param.mutable_till()->set_seconds(200);
    param.mutable_instance_filter()->set_service_id(instanceFilter.mServiceID.GetValue().CStr());
    param.mutable_instance_filter()->set_instance(instanceFilter.mInstance.GetValue());

    auto result = aos::common::pbconvert::ConvertToAos(param);

    EXPECT_EQ(result.mLogID, aos::String(param.log_id().c_str()));
    EXPECT_EQ(result.mFilter.mFrom, aos::Time::Unix(100, 0));
    EXPECT_EQ(result.mFilter.mTill, aos::Time::Unix(200, 0));

    EXPECT_EQ(result.mFilter.mInstanceFilter, instanceFilter);
}

TEST_F(PBConvertSMTest, ConvertInstanceCrashLogRequestToAos)
{
    ::servicemanager::v4::InstanceCrashLogRequest param;

    aos::cloudprotocol::InstanceFilter instanceFilter;
    instanceFilter.mServiceID.SetValue("service-id");
    instanceFilter.mInstance.SetValue(1);

    param.set_log_id("log-id");
    param.mutable_from()->set_seconds(100);
    param.mutable_till()->set_seconds(200);
    param.mutable_instance_filter()->set_service_id(instanceFilter.mServiceID.GetValue().CStr());
    param.mutable_instance_filter()->set_instance(instanceFilter.mInstance.GetValue());

    auto result = aos::common::pbconvert::ConvertToAos(param);

    EXPECT_EQ(result.mLogID, aos::String(param.log_id().c_str()));
    EXPECT_EQ(result.mFilter.mFrom, aos::Time::Unix(100, 0));
    EXPECT_EQ(result.mFilter.mTill, aos::Time::Unix(200, 0));

    EXPECT_EQ(result.mFilter.mInstanceFilter, instanceFilter);
}

TEST_F(PBConvertSMTest, ConvertSystemAlertToProto)
{
    aos::Time expectedTimestamp = aos::Time::Now();

    aos::cloudprotocol::AlertVariant param;

    aos::cloudprotocol::SystemAlert alert {expectedTimestamp};
    alert.mMessage = "test-message";

    param.SetValue<aos::cloudprotocol::SystemAlert>(alert);

    ::servicemanager::v4::Alert result = aos::common::pbconvert::ConvertToProto(param);

    ASSERT_TRUE(result.has_system_alert());

    EXPECT_EQ(result.tag(), "systemAlert");
    CompareTimestamps(alert.mTimestamp, result.timestamp());

    const auto& pbAlert = result.system_alert();
    EXPECT_EQ(aos::String(pbAlert.message().c_str()), alert.mMessage);
}

TEST_F(PBConvertSMTest, ConvertCoreAlertToProto)
{
    aos::Time expectedTimestamp = aos::Time::Now();

    aos::cloudprotocol::AlertVariant param;

    aos::cloudprotocol::CoreAlert alert {expectedTimestamp};
    alert.mMessage       = "test-message";
    alert.mCoreComponent = aos::cloudprotocol::CoreComponentEnum::eCommunicationManager;

    param.SetValue<aos::cloudprotocol::CoreAlert>(alert);

    ::servicemanager::v4::Alert result = aos::common::pbconvert::ConvertToProto(param);

    ASSERT_TRUE(result.has_core_alert());

    EXPECT_EQ(result.tag(), "coreAlert");
    CompareTimestamps(alert.mTimestamp, result.timestamp());

    const auto& pbAlert = result.core_alert();

    EXPECT_EQ(aos::String(pbAlert.message().c_str()), alert.mMessage);
    EXPECT_EQ(aos::String(pbAlert.core_component().c_str()), alert.mCoreComponent.ToString());
}

TEST_F(PBConvertSMTest, ConvertSystemQuotaAlertToProto)
{
    aos::Time expectedTimestamp = aos::Time::Now();

    aos::cloudprotocol::AlertVariant param;

    aos::cloudprotocol::SystemQuotaAlert alert {expectedTimestamp};
    alert.mParameter = "test-param";
    alert.mValue     = 10;

    param.SetValue<aos::cloudprotocol::SystemQuotaAlert>(alert);

    ::servicemanager::v4::Alert result = aos::common::pbconvert::ConvertToProto(param);

    ASSERT_TRUE(result.has_system_quota_alert());

    EXPECT_EQ(result.tag(), "systemQuotaAlert");
    CompareTimestamps(alert.mTimestamp, result.timestamp());

    const auto& pbAlert = result.system_quota_alert();

    EXPECT_EQ(aos::String(pbAlert.parameter().c_str()), alert.mParameter);
    EXPECT_EQ(pbAlert.value(), alert.mValue);
}

TEST_F(PBConvertSMTest, ConvertInstanceQuotaAlertToProto)
{
    aos::Time expectedTimestamp = aos::Time::Now();

    aos::cloudprotocol::AlertVariant param;

    aos::cloudprotocol::InstanceQuotaAlert alert {expectedTimestamp};
    alert.mParameter = "test-param";
    alert.mValue     = 10;
    alert.mStatus    = aos::cloudprotocol::AlertStatusEnum::eContinue;

    param.SetValue<aos::cloudprotocol::InstanceQuotaAlert>(alert);

    ::servicemanager::v4::Alert result = aos::common::pbconvert::ConvertToProto(param);

    ASSERT_TRUE(result.has_instance_quota_alert());

    EXPECT_EQ(result.tag(), "instanceQuotaAlert");
    CompareTimestamps(alert.mTimestamp, result.timestamp());

    const auto& pbAlert = result.instance_quota_alert();

    EXPECT_EQ(aos::String(pbAlert.parameter().c_str()), alert.mParameter);
    EXPECT_EQ(pbAlert.value(), alert.mValue);
    EXPECT_EQ(aos::String(pbAlert.status().c_str()), alert.mStatus);
}

TEST_F(PBConvertSMTest, ConvertDeviceAllocateAlertToProto)
{
    aos::Time expectedTimestamp = aos::Time::Now();

    aos::cloudprotocol::AlertVariant param;

    aos::cloudprotocol::DeviceAllocateAlert alert {expectedTimestamp};
    alert.mDevice  = "test-device";
    alert.mMessage = "test-message";

    param.SetValue<aos::cloudprotocol::DeviceAllocateAlert>(alert);

    ::servicemanager::v4::Alert result = aos::common::pbconvert::ConvertToProto(param);

    ASSERT_TRUE(result.has_device_allocate_alert());

    EXPECT_EQ(result.tag(), "deviceAllocateAlert");
    CompareTimestamps(alert.mTimestamp, result.timestamp());

    const auto& pbAlert = result.device_allocate_alert();

    EXPECT_EQ(aos::String(pbAlert.device().c_str()), alert.mDevice);
    EXPECT_EQ(aos::String(pbAlert.message().c_str()), alert.mMessage);
}

TEST_F(PBConvertSMTest, ConvertResourceValidateAlertToProto)
{
    aos::Time  expectedTimestamp = aos::Time::Now();
    aos::Error expectedErrors[]  = {
        aos::Error {aos::ErrorEnum::eFailed, "failed error"},
        aos::Error {aos::ErrorEnum::eRuntime, "runtime error"},
        aos::Error {aos::ErrorEnum::eNone},
    };

    aos::cloudprotocol::AlertVariant param;

    aos::cloudprotocol::ResourceValidateAlert alert {expectedTimestamp};
    alert.mName   = "test-name";
    alert.mErrors = aos::Array<aos::Error>(expectedErrors, std::size(expectedErrors));

    param.SetValue<aos::cloudprotocol::ResourceValidateAlert>(alert);

    ::servicemanager::v4::Alert result = aos::common::pbconvert::ConvertToProto(param);

    ASSERT_TRUE(result.has_resource_validate_alert());

    EXPECT_EQ(result.tag(), "resourceValidateAlert");
    CompareTimestamps(alert.mTimestamp, result.timestamp());

    const auto& pbAlert = result.resource_validate_alert();
    EXPECT_EQ(aos::String(pbAlert.name().c_str()), alert.mName);

    ASSERT_EQ(pbAlert.errors_size(), std::size(expectedErrors));

    for (size_t i = 0; i < std::size(expectedErrors); ++i) {
        const auto& pbError = pbAlert.errors(i);
        const auto& error   = expectedErrors[i];

        EXPECT_EQ(pbError.aos_code(), static_cast<int32_t>(error.Value()));
        EXPECT_EQ(aos::String(pbError.message().c_str()), error.Message());
    }
}

TEST_F(PBConvertSMTest, ConvertDownloadAlertToProto)
{
    aos::Time expectedTimestamp = aos::Time::Now();

    aos::cloudprotocol::AlertVariant param;

    aos::cloudprotocol::DownloadAlert alert {expectedTimestamp};

    param.SetValue<aos::cloudprotocol::DownloadAlert>(alert);

    ::servicemanager::v4::Alert result = aos::common::pbconvert::ConvertToProto(param);

    EXPECT_EQ(result.AlertItem_case(), ::servicemanager::v4::Alert::ALERTITEM_NOT_SET);
    EXPECT_EQ(result.tag(), "downloadProgressAlert");

    CompareTimestamps(alert.mTimestamp, result.timestamp());
}

TEST_F(PBConvertSMTest, ConvertServiceInstanceAlertToProto)
{
    aos::Time expectedTimestamp = aos::Time::Now();

    aos::cloudprotocol::AlertVariant param;

    aos::cloudprotocol::ServiceInstanceAlert alert {expectedTimestamp};

    param.SetValue<aos::cloudprotocol::ServiceInstanceAlert>(alert);

    ::servicemanager::v4::Alert result = aos::common::pbconvert::ConvertToProto(param);

    EXPECT_EQ(result.AlertItem_case(), ::servicemanager::v4::Alert::ALERTITEM_NOT_SET);
    EXPECT_EQ(result.tag(), "serviceInstanceAlert");

    CompareTimestamps(alert.mTimestamp, result.timestamp());
}
