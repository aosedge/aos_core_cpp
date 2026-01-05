/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gmock/gmock.h>

#include <core/common/tests/utils/log.hpp>

#include <common/pbconvert/sm.hpp>

using namespace testing;

namespace aos::common::pbconvert {

namespace {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

servicemanager::v5::Alert CreateSystemQuotaAlert()
{
    servicemanager::v5::Alert alert;

    alert.mutable_timestamp()->set_seconds(1000);
    alert.mutable_timestamp()->set_nanos(500);

    auto* systemQuota = alert.mutable_system_quota_alert();
    systemQuota->set_parameter("ram");
    systemQuota->set_value(85);
    systemQuota->set_status("raise");

    return alert;
}

servicemanager::v5::Alert CreateInstanceQuotaAlert()
{
    servicemanager::v5::Alert alert;

    alert.mutable_timestamp()->set_seconds(2000);
    alert.mutable_timestamp()->set_nanos(100);

    auto* instanceQuota = alert.mutable_instance_quota_alert();
    instanceQuota->mutable_instance()->set_item_id("service1");
    instanceQuota->mutable_instance()->set_subject_id("user1");
    instanceQuota->mutable_instance()->set_instance(0);
    instanceQuota->set_parameter("cpu");
    instanceQuota->set_value(95.0);
    instanceQuota->set_status("continue");

    return alert;
}

servicemanager::v5::Alert CreateResourceAllocateAlert()
{
    servicemanager::v5::Alert alert;

    alert.mutable_timestamp()->set_seconds(3000);

    auto* resourceAlert = alert.mutable_resource_allocate_alert();
    resourceAlert->mutable_instance()->set_item_id("service2");
    resourceAlert->mutable_instance()->set_subject_id("user2");
    resourceAlert->mutable_instance()->set_instance(1);
    resourceAlert->set_resource("gpu");
    resourceAlert->set_message("resource allocation failed");

    return alert;
}

servicemanager::v5::Alert CreateSystemAlert()
{
    servicemanager::v5::Alert alert;

    alert.mutable_timestamp()->set_seconds(4000);

    auto* systemAlert = alert.mutable_system_alert();
    systemAlert->set_message("system error occurred");

    return alert;
}

servicemanager::v5::Alert CreateCoreAlert()
{
    servicemanager::v5::Alert alert;

    alert.mutable_timestamp()->set_seconds(5000);

    auto* coreAlert = alert.mutable_core_alert();
    coreAlert->set_core_component("CM");
    coreAlert->set_message("core component error");

    return alert;
}

servicemanager::v5::Alert CreateInstanceAlert()
{
    servicemanager::v5::Alert alert;

    alert.mutable_timestamp()->set_seconds(6000);

    auto* instanceAlert = alert.mutable_instance_alert();
    instanceAlert->mutable_instance()->set_item_id("service3");
    instanceAlert->mutable_instance()->set_subject_id("user3");
    instanceAlert->mutable_instance()->set_instance(2);
    instanceAlert->set_service_version("3.0.0");
    instanceAlert->set_message("instance crashed");

    return alert;
}

} // namespace

class PBConvertSMTest : public Test {
public:
    void SetUp() override { tests::utils::InitLog(); }
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(PBConvertSMTest, ConvertErrorInfoFromProto)
{
    ::common::v2::ErrorInfo grpcError;

    grpcError.set_aos_code(static_cast<int32_t>(ErrorEnum::eFailed));
    grpcError.set_exit_code(0);
    grpcError.set_message("test error message");

    auto result = ConvertFromProto(grpcError);

    EXPECT_EQ(result.Value(), ErrorEnum::eFailed);
    EXPECT_EQ(String(result.Message()), String("test error message"));
}

TEST_F(PBConvertSMTest, ConvertErrorInfoFromProtoWithoutAosCode)
{
    ::common::v2::ErrorInfo grpcError;

    grpcError.set_aos_code(0);
    grpcError.set_exit_code(42);
    grpcError.set_message("exit code error");

    auto result = ConvertFromProto(grpcError);
    EXPECT_EQ(result.Errno(), 42);
    EXPECT_EQ(String(result.Message()), String("exit code error"));
}

TEST_F(PBConvertSMTest, ConvertNodeConfigStatusFromProto)
{
    servicemanager::v5::NodeConfigStatus grpcStatus;

    grpcStatus.set_version("1.0.0");
    grpcStatus.set_state("installed");

    NodeConfigStatus aosStatus;

    auto err = ConvertFromProto(grpcStatus, aosStatus);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    EXPECT_EQ(aosStatus.mVersion, String("1.0.0"));
    EXPECT_EQ(aosStatus.mState, UnitConfigStateEnum::eInstalled);
    EXPECT_TRUE(aosStatus.mError.IsNone());
}

TEST_F(PBConvertSMTest, ConvertNodeConfigStatusFromProtoWithError)
{
    servicemanager::v5::NodeConfigStatus grpcStatus;

    grpcStatus.set_version("2.0.0");
    grpcStatus.set_state("failed");

    auto* error = grpcStatus.mutable_error();
    error->set_aos_code(static_cast<int32_t>(ErrorEnum::eRuntime));
    error->set_message("config error");

    NodeConfigStatus aosStatus;

    auto err = ConvertFromProto(grpcStatus, aosStatus);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    EXPECT_EQ(aosStatus.mVersion, String("2.0.0"));
    EXPECT_EQ(aosStatus.mState, UnitConfigStateEnum::eFailed);
    EXPECT_FALSE(aosStatus.mError.IsNone());
    EXPECT_EQ(aosStatus.mError.Value(), ErrorEnum::eRuntime);
}

TEST_F(PBConvertSMTest, ConvertInstanceStatusFromProto)
{
    servicemanager::v5::InstanceStatus grpcStatus;

    grpcStatus.mutable_instance()->set_item_id("service1");
    grpcStatus.mutable_instance()->set_subject_id("user1");
    grpcStatus.mutable_instance()->set_instance(0);
    grpcStatus.set_version("2.0.0");
    grpcStatus.set_runtime_id("runc");
    grpcStatus.set_state("active");

    auto* envVarStatus1 = grpcStatus.add_env_vars();
    envVarStatus1->set_name("VAR1");
    auto* error1 = envVarStatus1->mutable_error();
    error1->set_aos_code(0);

    auto* envVarStatus2 = grpcStatus.add_env_vars();
    envVarStatus2->set_name("VAR2");
    auto* error2 = envVarStatus2->mutable_error();
    error2->set_aos_code(static_cast<int32_t>(ErrorEnum::eFailed));
    error2->set_message("env var error");

    InstanceStatus aosStatus;

    auto err = ConvertFromProto(grpcStatus, String("node1"), aosStatus);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    EXPECT_EQ(aosStatus.mItemID, String("service1"));
    EXPECT_EQ(aosStatus.mSubjectID, String("user1"));
    EXPECT_EQ(aosStatus.mInstance, 0);
    EXPECT_EQ(aosStatus.mVersion, String("2.0.0"));
    EXPECT_EQ(aosStatus.mNodeID, String("node1"));
    EXPECT_EQ(aosStatus.mRuntimeID, String("runc"));
    EXPECT_EQ(aosStatus.mState, InstanceStateEnum::eActive);
    EXPECT_TRUE(aosStatus.mError.IsNone());

    ASSERT_EQ(aosStatus.mEnvVarsStatuses.Size(), 2);
    EXPECT_EQ(aosStatus.mEnvVarsStatuses[0].mName, String("VAR1"));
    EXPECT_TRUE(aosStatus.mEnvVarsStatuses[0].mError.IsNone());
    EXPECT_EQ(aosStatus.mEnvVarsStatuses[1].mName, String("VAR2"));
    EXPECT_FALSE(aosStatus.mEnvVarsStatuses[1].mError.IsNone());
    EXPECT_EQ(aosStatus.mEnvVarsStatuses[1].mError.Value(), ErrorEnum::eFailed);
}

TEST_F(PBConvertSMTest, ConvertAverageMonitoringFromProto)
{
    servicemanager::v5::AverageMonitoring grpcMonitoring;

    auto* nodeData = grpcMonitoring.mutable_node_monitoring();
    nodeData->set_ram(2048);
    nodeData->set_cpu(75);
    nodeData->set_download(300);
    nodeData->set_upload(400);

    auto* partition = nodeData->add_partitions();
    partition->set_name("part1");
    partition->set_used_size(1024);

    auto* inst = grpcMonitoring.add_instances_monitoring();
    inst->mutable_instance()->set_item_id("item1");
    inst->mutable_instance()->set_subject_id("subj1");
    inst->mutable_instance()->set_instance(5);
    inst->set_runtime_id("crun");
    inst->mutable_monitoring_data()->set_ram(512);
    inst->mutable_monitoring_data()->set_cpu(25.0);

    monitoring::NodeMonitoringData aosMonitoring;

    auto err = ConvertFromProto(grpcMonitoring, String("node1"), aosMonitoring);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    EXPECT_EQ(aosMonitoring.mNodeID, String("node1"));
    EXPECT_EQ(aosMonitoring.mMonitoringData.mRAM, 2048);
    EXPECT_EQ(aosMonitoring.mMonitoringData.mCPU, 75);
    ASSERT_EQ(aosMonitoring.mMonitoringData.mPartitions.Size(), 1);
    EXPECT_EQ(aosMonitoring.mMonitoringData.mPartitions[0].mName, String("part1"));

    ASSERT_EQ(aosMonitoring.mInstances.Size(), 1);
    EXPECT_EQ(aosMonitoring.mInstances[0].mInstanceIdent.mItemID, String("item1"));
    EXPECT_EQ(aosMonitoring.mInstances[0].mRuntimeID, String("crun"));
}

TEST_F(PBConvertSMTest, ConvertInstantMonitoringFromProto)
{
    servicemanager::v5::InstantMonitoring grpcMonitoring;

    auto* nodeData = grpcMonitoring.mutable_node_monitoring();
    nodeData->set_ram(4096);
    nodeData->set_cpu(80.0);

    monitoring::NodeMonitoringData aosMonitoring;

    auto err = ConvertFromProto(grpcMonitoring, String("node2"), aosMonitoring);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    EXPECT_EQ(aosMonitoring.mNodeID, String("node2"));
    EXPECT_EQ(aosMonitoring.mMonitoringData.mRAM, 4096);
    EXPECT_EQ(aosMonitoring.mMonitoringData.mCPU, 80.0);
}

TEST_F(PBConvertSMTest, ConvertLogDataFromProto)
{
    servicemanager::v5::LogData grpcLog;

    grpcLog.set_correlation_id("log-123");
    grpcLog.set_part_count(5);
    grpcLog.set_part(2);
    grpcLog.set_data("log content data");
    grpcLog.set_status("ok");

    PushLog aosPushLog;

    auto err = ConvertFromProto(grpcLog, String("node-1"), aosPushLog);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    EXPECT_EQ(aosPushLog.mCorrelationID, String("log-123"));
    EXPECT_EQ(aosPushLog.mNodeID, String("node-1"));
    EXPECT_EQ(aosPushLog.mPartsCount, 5);
    EXPECT_EQ(aosPushLog.mPart, 2);
    EXPECT_EQ(aosPushLog.mContent, String("log content data"));
    EXPECT_EQ(aosPushLog.mStatus, LogStatusType::Enum::eOK);
    EXPECT_TRUE(aosPushLog.mError.IsNone());
}

TEST_F(PBConvertSMTest, ConvertLogDataFromProtoWithError)
{
    servicemanager::v5::LogData grpcLog;

    grpcLog.set_correlation_id("log-456");
    grpcLog.set_part_count(1);
    grpcLog.set_part(1);
    grpcLog.set_data("");
    grpcLog.set_status("error");

    auto* error = grpcLog.mutable_error();

    error->set_aos_code(static_cast<int32_t>(ErrorEnum::eFailed));
    error->set_message("log retrieval failed");

    PushLog aosPushLog;

    auto err = ConvertFromProto(grpcLog, String("node-2"), aosPushLog);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    EXPECT_EQ(aosPushLog.mStatus, LogStatusEnum::eError);
    EXPECT_FALSE(aosPushLog.mError.IsNone());
    EXPECT_EQ(aosPushLog.mError.Value(), ErrorEnum::eFailed);
}

TEST_F(PBConvertSMTest, ConvertUpdateNetworksToProto)
{
    StaticArray<UpdateNetworkParameters, 2> networkParams;

    UpdateNetworkParameters param1;
    UpdateNetworkParameters param2;

    param1.mNetworkID = "net1";
    param1.mSubnet    = "10.0.1.0/24";
    param1.mIP        = "10.0.1.5";
    param1.mVlanID    = 10;
    networkParams.PushBack(param1);

    param2.mNetworkID = "net2";
    param2.mSubnet    = "10.0.2.0/24";
    param2.mIP        = "10.0.2.5";
    param2.mVlanID    = 20;
    networkParams.PushBack(param2);

    servicemanager::v5::UpdateNetworks result;

    auto err = ConvertToProto(networkParams, result);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    ASSERT_EQ(result.networks_size(), 2);
    EXPECT_EQ(result.networks(0).network_id(), "net1");
    EXPECT_EQ(result.networks(1).network_id(), "net2");
}

TEST_F(PBConvertSMTest, ConvertRequestLogToSystemLogRequest)
{
    RequestLog log;

    log.mCorrelationID = "log-id-1";
    log.mFilter.mFrom.SetValue(Time::Unix(1000, 0));
    log.mFilter.mTill.SetValue(Time::Unix(2000, 0));

    servicemanager::v5::SystemLogRequest result;

    auto err = ConvertToProto(log, result);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    EXPECT_EQ(result.correlation_id(), "log-id-1");
    EXPECT_TRUE(result.has_from());
    EXPECT_EQ(result.from().seconds(), 1000);
    EXPECT_TRUE(result.has_till());
    EXPECT_EQ(result.till().seconds(), 2000);
}

TEST_F(PBConvertSMTest, ConvertRequestLogToInstanceLogRequest)
{
    RequestLog log;

    log.mCorrelationID = "log-id-2";
    log.mFilter.mItemID.SetValue("item1");
    log.mFilter.mSubjectID.SetValue("subject1");
    log.mFilter.mInstance.SetValue(3);

    servicemanager::v5::InstanceLogRequest result;

    auto err = ConvertToProto(log, result);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    EXPECT_EQ(result.correlation_id(), "log-id-2");
    EXPECT_TRUE(result.has_filter());
    EXPECT_EQ(result.filter().item_id(), "item1");
    EXPECT_EQ(result.filter().subject_id(), "subject1");
    EXPECT_EQ(result.filter().instance(), 3);
}

TEST_F(PBConvertSMTest, ConvertRequestLogToInstanceCrashLogRequest)
{
    RequestLog log;

    log.mCorrelationID = "crash-log-1";
    log.mFilter.mItemID.SetValue("crashed-service");
    log.mFilter.mFrom.SetValue(Time::Unix(5000, 0));

    servicemanager::v5::InstanceCrashLogRequest result;

    auto err = ConvertToProto(log, result);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    EXPECT_EQ(result.correlation_id(), "crash-log-1");
    EXPECT_TRUE(result.has_filter());
    EXPECT_EQ(result.filter().item_id(), "crashed-service");
    EXPECT_TRUE(result.has_from());
    EXPECT_EQ(result.from().seconds(), 5000);
}

TEST_F(PBConvertSMTest, ConvertSystemQuotaAlertFromProto)
{
    auto         grpcAlert = CreateSystemQuotaAlert();
    AlertVariant alertItem;

    auto err = ConvertFromProto(grpcAlert, String("test-node"), alertItem);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    const auto& alert = alertItem.GetValue<SystemQuotaAlert>();
    EXPECT_EQ(alert.mTimestamp.UnixTime().tv_sec, 1000);
    EXPECT_EQ(alert.mNodeID, String("test-node"));
    EXPECT_EQ(alert.mParameter, String("ram"));
    EXPECT_EQ(alert.mValue, 85);
    EXPECT_EQ(alert.mState, QuotaAlertStateEnum::eRaise);
}

TEST_F(PBConvertSMTest, ConvertInstanceQuotaAlertFromProto)
{
    auto         grpcAlert = CreateInstanceQuotaAlert();
    AlertVariant alertItem;

    auto err = ConvertFromProto(grpcAlert, String("test-node"), alertItem);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    const auto& alert = alertItem.GetValue<InstanceQuotaAlert>();
    EXPECT_EQ(alert.mTimestamp.UnixTime().tv_sec, 2000);
    EXPECT_EQ(alert.mItemID, String("service1"));
    EXPECT_EQ(alert.mSubjectID, String("user1"));
    EXPECT_EQ(alert.mInstance, 0);
    EXPECT_EQ(alert.mParameter, String("cpu"));
    EXPECT_EQ(alert.mValue, 95.0);
    EXPECT_EQ(alert.mState, QuotaAlertStateEnum::eContinue);
}

TEST_F(PBConvertSMTest, ConvertResourceAllocateAlertFromProto)
{
    auto         grpcAlert = CreateResourceAllocateAlert();
    AlertVariant alertItem;

    auto err = ConvertFromProto(grpcAlert, String("test-node"), alertItem);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    const auto& alert = alertItem.GetValue<ResourceAllocateAlert>();
    EXPECT_EQ(alert.mTimestamp.UnixTime().tv_sec, 3000);
    EXPECT_EQ(alert.mNodeID, String("test-node"));
    EXPECT_EQ(alert.mItemID, String("service2"));
    EXPECT_EQ(alert.mSubjectID, String("user2"));
    EXPECT_EQ(alert.mInstance, 1);
    EXPECT_EQ(alert.mResource, String("gpu"));
    EXPECT_EQ(alert.mMessage, String("resource allocation failed"));
}

TEST_F(PBConvertSMTest, ConvertSystemAlertFromProto)
{
    auto         grpcAlert = CreateSystemAlert();
    AlertVariant alertItem;

    auto err = ConvertFromProto(grpcAlert, String("test-node"), alertItem);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    const auto& alert = alertItem.GetValue<SystemAlert>();
    EXPECT_EQ(alert.mTimestamp.UnixTime().tv_sec, 4000);
    EXPECT_EQ(alert.mNodeID, String("test-node"));
    EXPECT_EQ(alert.mMessage, String("system error occurred"));
}

TEST_F(PBConvertSMTest, ConvertCoreAlertFromProto)
{
    auto         grpcAlert = CreateCoreAlert();
    AlertVariant alertItem;

    auto err = ConvertFromProto(grpcAlert, String("test-node"), alertItem);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    const auto& alert = alertItem.GetValue<CoreAlert>();
    EXPECT_EQ(alert.mTimestamp.UnixTime().tv_sec, 5000);
    EXPECT_EQ(alert.mNodeID, String("test-node"));
    EXPECT_EQ(alert.mCoreComponent, CoreComponentEnum::eCM);
    EXPECT_EQ(alert.mMessage, String("core component error"));
}

TEST_F(PBConvertSMTest, ConvertInstanceAlertFromProto)
{
    auto         grpcAlert = CreateInstanceAlert();
    AlertVariant alertItem;

    auto err = ConvertFromProto(grpcAlert, String("test-node"), alertItem);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    const auto& alert = alertItem.GetValue<InstanceAlert>();
    EXPECT_EQ(alert.mTimestamp.UnixTime().tv_sec, 6000);
    EXPECT_EQ(alert.mItemID, String("service3"));
    EXPECT_EQ(alert.mSubjectID, String("user3"));
    EXPECT_EQ(alert.mInstance, 2);
    EXPECT_EQ(alert.mVersion, String("3.0.0"));
    EXPECT_EQ(alert.mMessage, String("instance crashed"));
}

TEST_F(PBConvertSMTest, ConvertUpdateInstancesToProto)
{
    StaticArray<InstanceInfo, 2> stopInstances;
    StaticArray<InstanceInfo, 2> startInstances;

    InstanceInfo stop1;

    stop1.mItemID    = "old-service";
    stop1.mSubjectID = "user1";
    stop1.mInstance  = 0;
    stopInstances.PushBack(stop1);

    InstanceInfo start1;

    start1.mItemID      = "new-service";
    start1.mSubjectID   = "user1";
    start1.mInstance    = 0;
    start1.mRuntimeID   = "runc";
    start1.mOwnerID     = "owner1";
    start1.mUID         = 1000;
    start1.mGID         = 1000;
    start1.mPriority    = 50;
    start1.mStoragePath = "/storage";
    start1.mStatePath   = "/state";

    EnvVar envVar1;
    envVar1.mName  = "ENV_VAR1";
    envVar1.mValue = "value1";
    start1.mEnvVars.PushBack(envVar1);

    EnvVar envVar2;
    envVar2.mName  = "ENV_VAR2";
    envVar2.mValue = "value2";
    start1.mEnvVars.PushBack(envVar2);

    InstanceNetworkParameters netParams;

    netParams.mNetworkID = "net1";
    netParams.mIP        = "10.0.0.10";
    netParams.mSubnet    = "10.0.0.0/24";
    start1.mNetworkParameters.SetValue(netParams);

    InstanceMonitoringParams monitoringParams;

    monitoringParams.mAlertRules.EmplaceValue();
    monitoringParams.mAlertRules->mRAM.EmplaceValue(AlertRulePercents {120 * Time::cSeconds, 80.0, 95.0});
    monitoringParams.mAlertRules->mCPU.EmplaceValue(AlertRulePercents {20 * Time::cSeconds, 80.0, 95.0});
    monitoringParams.mAlertRules->mPartitions.EmplaceBack(
        PartitionAlertRule {300 * Time::cSeconds, 70.0, 90.0, "part1"});
    monitoringParams.mAlertRules->mPartitions.EmplaceBack(
        PartitionAlertRule {300 * Time::cSeconds, 70.0, 90.0, "part2"});
    monitoringParams.mAlertRules->mDownload.EmplaceValue(AlertRulePoints {180 * Time::cSeconds, 1000, 2000});
    monitoringParams.mAlertRules->mUpload.EmplaceValue(AlertRulePoints {10 * Time::cSeconds, 2000, 3000});
    start1.mMonitoringParams.SetValue(monitoringParams);

    startInstances.PushBack(start1);

    servicemanager::v5::UpdateInstances result;

    auto err = ConvertToProto(stopInstances, startInstances, result);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    ASSERT_EQ(result.stop_instances_size(), 1);
    EXPECT_EQ(result.stop_instances(0).item_id(), "old-service");

    ASSERT_EQ(result.start_instances_size(), 1);
    EXPECT_EQ(result.start_instances(0).instance().item_id(), "new-service");
    EXPECT_EQ(result.start_instances(0).runtime_id(), "runc");
    EXPECT_EQ(result.start_instances(0).uid(), 1000);

    ASSERT_EQ(result.start_instances(0).env_vars_size(), 2);
    EXPECT_EQ(result.start_instances(0).env_vars(0).name(), "ENV_VAR1");
    EXPECT_EQ(result.start_instances(0).env_vars(0).value(), "value1");
    EXPECT_EQ(result.start_instances(0).env_vars(1).name(), "ENV_VAR2");
    EXPECT_EQ(result.start_instances(0).env_vars(1).value(), "value2");

    ASSERT_TRUE(result.start_instances(0).has_network_parameters());
    EXPECT_EQ(result.start_instances(0).network_parameters().network_id(), "net1");
    EXPECT_EQ(result.start_instances(0).network_parameters().ip(), "10.0.0.10");
    EXPECT_EQ(result.start_instances(0).network_parameters().subnet(), "10.0.0.0/24");

    ASSERT_TRUE(result.start_instances(0).has_monitoring_parameters());
    ASSERT_TRUE(result.start_instances(0).monitoring_parameters().has_alert_rules());
    ASSERT_TRUE(result.start_instances(0).monitoring_parameters().alert_rules().has_ram());
    EXPECT_EQ(result.start_instances(0).monitoring_parameters().alert_rules().ram().duration().seconds(), 120);
    EXPECT_EQ(result.start_instances(0).monitoring_parameters().alert_rules().ram().min_threshold(), 80.0);
    EXPECT_EQ(result.start_instances(0).monitoring_parameters().alert_rules().ram().max_threshold(), 95.0);
    ASSERT_TRUE(result.start_instances(0).monitoring_parameters().alert_rules().has_cpu());
    EXPECT_EQ(result.start_instances(0).monitoring_parameters().alert_rules().cpu().duration().seconds(), 20);
    EXPECT_EQ(result.start_instances(0).monitoring_parameters().alert_rules().cpu().min_threshold(), 80.0);
    EXPECT_EQ(result.start_instances(0).monitoring_parameters().alert_rules().cpu().max_threshold(), 95.0);
    ASSERT_TRUE(result.start_instances(0).monitoring_parameters().alert_rules().has_download());
    EXPECT_EQ(result.start_instances(0).monitoring_parameters().alert_rules().download().duration().seconds(), 180);
    EXPECT_EQ(result.start_instances(0).monitoring_parameters().alert_rules().download().min_threshold(), 1000);
    EXPECT_EQ(result.start_instances(0).monitoring_parameters().alert_rules().download().max_threshold(), 2000);
    ASSERT_TRUE(result.start_instances(0).monitoring_parameters().alert_rules().has_upload());
    EXPECT_EQ(result.start_instances(0).monitoring_parameters().alert_rules().upload().duration().seconds(), 10);
    EXPECT_EQ(result.start_instances(0).monitoring_parameters().alert_rules().upload().min_threshold(), 2000);
    EXPECT_EQ(result.start_instances(0).monitoring_parameters().alert_rules().upload().max_threshold(), 3000);
    ASSERT_EQ(result.start_instances(0).monitoring_parameters().alert_rules().partitions_size(), 2);
    EXPECT_EQ(result.start_instances(0).monitoring_parameters().alert_rules().partitions(0).name(), "part1");
    EXPECT_EQ(result.start_instances(0).monitoring_parameters().alert_rules().partitions(0).duration().seconds(), 300);
    EXPECT_EQ(result.start_instances(0).monitoring_parameters().alert_rules().partitions(0).min_threshold(), 70.0);
    EXPECT_EQ(result.start_instances(0).monitoring_parameters().alert_rules().partitions(0).max_threshold(), 90.0);
    EXPECT_EQ(result.start_instances(0).monitoring_parameters().alert_rules().partitions(1).name(), "part2");
    EXPECT_EQ(result.start_instances(0).monitoring_parameters().alert_rules().partitions(1).duration().seconds(), 300);
    EXPECT_EQ(result.start_instances(0).monitoring_parameters().alert_rules().partitions(1).min_threshold(), 70.0);
    EXPECT_EQ(result.start_instances(0).monitoring_parameters().alert_rules().partitions(1).max_threshold(), 90.0);
}

TEST_F(PBConvertSMTest, ConvertSMInfoFromProto)
{
    servicemanager::v5::SMInfo grpcInfo;

    grpcInfo.set_node_id("sm-node-1");

    auto* resource = grpcInfo.add_resources();
    resource->set_name("disk");
    resource->set_shared_count(2);

    auto* runtime = grpcInfo.add_runtimes();
    runtime->set_runtime_id("runc");
    runtime->set_type("container");
    runtime->set_max_dmips(1000);
    runtime->set_allowed_dmips(800);
    runtime->set_total_ram(4096);
    runtime->set_allowed_ram(2048);
    runtime->set_max_instances(10);
    auto* archInfo = runtime->mutable_arch_info();
    archInfo->set_architecture("arm64");
    archInfo->set_variant("v7");
    auto* osInfo = runtime->mutable_os_info();
    osInfo->set_os("linux");
    osInfo->set_version("5.10");

    cm::nodeinfoprovider::SMInfo aosInfo;

    auto err = ConvertFromProto(grpcInfo, aosInfo);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    EXPECT_EQ(aosInfo.mNodeID, String("sm-node-1"));
    ASSERT_EQ(aosInfo.mResources.Size(), 1);
    EXPECT_EQ(aosInfo.mResources[0].mName, String("disk"));
    EXPECT_EQ(aosInfo.mResources[0].mSharedCount, 2);
    ASSERT_EQ(aosInfo.mRuntimes.Size(), 1);
    EXPECT_EQ(aosInfo.mRuntimes[0].mRuntimeID, String("runc"));
    EXPECT_EQ(aosInfo.mRuntimes[0].mRuntimeType, String("container"));
    EXPECT_EQ(aosInfo.mRuntimes[0].mMaxDMIPS, 1000);
    EXPECT_EQ(aosInfo.mRuntimes[0].mMaxInstances, 10);
    EXPECT_EQ(aosInfo.mRuntimes[0].mArchInfo.mArchitecture, "arm64");
    EXPECT_EQ(*aosInfo.mRuntimes[0].mArchInfo.mVariant, "v7");
    EXPECT_EQ(aosInfo.mRuntimes[0].mOSInfo.mOS, "linux");
    EXPECT_EQ(*aosInfo.mRuntimes[0].mOSInfo.mVersion, "5.10");
}

TEST_F(PBConvertSMTest, ConvertNodeConfigToCheckNodeConfigProto)
{
    static constexpr auto cExpectedNodeConfigJSON
        = R"({"version":"2.5.0","nodeType":"main","nodeId":"config-node","labels":[],"priority":0})";

    NodeConfig config;

    config.mNodeID   = "config-node";
    config.mNodeType = "main";
    config.mVersion  = "2.5.0";

    servicemanager::v5::CheckNodeConfig result;

    auto err = ConvertToProto(config, result);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    EXPECT_EQ(result.version(), "2.5.0");
    EXPECT_EQ(result.node_config(), cExpectedNodeConfigJSON);
}

TEST_F(PBConvertSMTest, ConvertNodeConfigToSetNodeConfigProto)
{
    static constexpr auto cExpectedNodeConfigJSON
        = R"({"version":"3.0.0","nodeType":"main","nodeId":"config-node","labels":[],"priority":0})";

    NodeConfig config;

    config.mNodeID   = "config-node";
    config.mNodeType = "main";
    config.mVersion  = "3.0.0";

    servicemanager::v5::SetNodeConfig result;

    auto err = ConvertToProto(config, result);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    EXPECT_EQ(result.version(), "3.0.0");
    EXPECT_EQ(result.node_config(), cExpectedNodeConfigJSON);
}

} // namespace aos::common::pbconvert
