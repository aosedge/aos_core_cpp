/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <iostream>
#include <mutex>
#include <optional>

#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>

#include <common/iamclient/publicservicehandler.hpp>
#include <common/tests/mocks/iamclientmock.hpp>
#include <common/utils/channel.hpp>

#include <mp/cmclient/cmclient.hpp>
#include <mp/config/config.hpp>

#include "stubs/smservice.hpp"

using namespace testing;

namespace aos::mp::cmclient {

namespace {

/***********************************************************************************************************************
 * Test utils
 **********************************************************************************************************************/

servicemanager::v4::SMOutgoingMessages CreateNodeConfigStatus()
{
    servicemanager::v4::SMOutgoingMessages unitConfigStatus;
    unitConfigStatus.mutable_node_config_status()->set_node_id("node_id");
    unitConfigStatus.mutable_node_config_status()->set_version("version");

    ::common::v1::ErrorInfo errorInfo;
    errorInfo.set_aos_code(1);
    errorInfo.set_exit_code(1);
    errorInfo.set_message("message");

    unitConfigStatus.mutable_node_config_status()->mutable_error()->CopyFrom(errorInfo);

    return unitConfigStatus;
}

servicemanager::v4::SMOutgoingMessages CreateRunInstancesStatus()
{
    servicemanager::v4::SMOutgoingMessages runInstancesStatus;

    servicemanager::v4::InstanceStatus instanceStatus;
    instanceStatus.set_service_version("service_version");
    instanceStatus.set_run_state("run_state");

    ::common::v1::InstanceIdent instanceIdent;
    instanceIdent.set_service_id("service_id");
    instanceIdent.set_subject_id("subject_id");
    instanceIdent.set_instance(1);
    instanceStatus.mutable_instance()->CopyFrom(instanceIdent);

    ::common::v1::ErrorInfo errorInfo;
    errorInfo.set_aos_code(1);
    errorInfo.set_exit_code(1);
    errorInfo.set_message("message");
    instanceStatus.mutable_error()->CopyFrom(errorInfo);
    runInstancesStatus.mutable_run_instances_status()->add_instances()->CopyFrom(instanceStatus);

    return runInstancesStatus;
}

servicemanager::v4::SMOutgoingMessages CreateUpdateInstancesStatus()
{
    servicemanager::v4::SMOutgoingMessages updateInstancesStatus;

    servicemanager::v4::InstanceStatus instanceStatus;
    instanceStatus.set_service_version("service_version");
    instanceStatus.set_run_state("run_state");

    ::common::v1::InstanceIdent instanceIdent;
    instanceIdent.set_service_id("service_id");
    instanceIdent.set_subject_id("subject_id");
    instanceIdent.set_instance(1);
    instanceStatus.mutable_instance()->CopyFrom(instanceIdent);

    ::common::v1::ErrorInfo errorInfo;
    errorInfo.set_aos_code(1);
    errorInfo.set_exit_code(1);
    errorInfo.set_message("message");
    instanceStatus.mutable_error()->CopyFrom(errorInfo);
    updateInstancesStatus.mutable_update_instances_status()->add_instances()->CopyFrom(instanceStatus);

    return updateInstancesStatus;
}

servicemanager::v4::SMOutgoingMessages CreateOverrideEnvVarStatus()
{
    servicemanager::v4::SMOutgoingMessages   overrideEnvVarStatus;
    servicemanager::v4::EnvVarInstanceStatus envVarInstanceStatus;

    servicemanager::v4::InstanceFilter instanceFilter;
    instanceFilter.set_service_id("service_id");
    instanceFilter.set_subject_id("subject_id");
    instanceFilter.set_instance(1);
    envVarInstanceStatus.mutable_instance_filter()->CopyFrom(instanceFilter);

    servicemanager::v4::EnvVarStatus envVarStatus;
    envVarStatus.set_name("name");

    ::common::v1::ErrorInfo errorInfo;
    errorInfo.set_aos_code(1);
    errorInfo.set_exit_code(1);
    errorInfo.set_message("message");
    envVarStatus.mutable_error()->CopyFrom(errorInfo);

    envVarInstanceStatus.mutable_statuses()->Add(std::move(envVarStatus));
    overrideEnvVarStatus.mutable_override_env_var_status()->add_env_vars_status()->CopyFrom(envVarInstanceStatus);

    return overrideEnvVarStatus;
}

servicemanager::v4::SMOutgoingMessages CreateLogData()
{
    servicemanager::v4::SMOutgoingMessages logData;

    servicemanager::v4::LogData logDataMsg;
    logDataMsg.set_log_id("log_id");
    logDataMsg.set_part_count(1);
    logDataMsg.set_part(1);
    logDataMsg.set_data("data");

    ::common::v1::ErrorInfo errorInfo;
    errorInfo.set_aos_code(1);
    errorInfo.set_exit_code(1);
    errorInfo.set_message("message");
    logDataMsg.mutable_error()->CopyFrom(errorInfo);

    logData.mutable_log()->CopyFrom(logDataMsg);

    return logData;
}

servicemanager::v4::SMOutgoingMessages CreateInstantMonitoring()
{
    servicemanager::v4::SMOutgoingMessages instantMonitoring;
    servicemanager::v4::InstantMonitoring  instantMonitoringMsg;

    servicemanager::v4::MonitoringData monitoringData;
    monitoringData.set_ram(1);
    monitoringData.set_cpu(1);
    monitoringData.set_download(1);
    monitoringData.set_upload(1);

    google::protobuf::Timestamp timestamp;
    timestamp.set_seconds(1);
    timestamp.set_nanos(1);
    monitoringData.mutable_timestamp()->CopyFrom(timestamp);

    servicemanager::v4::PartitionUsage partitionUsage;
    partitionUsage.set_name("name");
    partitionUsage.set_used_size(1);
    monitoringData.add_partitions()->CopyFrom(partitionUsage);

    instantMonitoringMsg.mutable_node_monitoring()->CopyFrom(monitoringData);

    servicemanager::v4::InstanceMonitoring instanceMonitoring;
    ::common::v1::InstanceIdent            instanceIdent;
    instanceIdent.set_service_id("service_id");
    instanceIdent.set_subject_id("subject_id");
    instanceIdent.set_instance(1);
    instanceMonitoring.mutable_instance()->CopyFrom(instanceIdent);
    instanceMonitoring.mutable_monitoring_data()->CopyFrom(monitoringData);

    instantMonitoringMsg.add_instances_monitoring()->CopyFrom(instanceMonitoring);

    instantMonitoring.mutable_instant_monitoring()->CopyFrom(instantMonitoringMsg);

    return instantMonitoring;
}

servicemanager::v4::SMOutgoingMessages CreateAverageMonitoring()
{
    servicemanager::v4::SMOutgoingMessages averageMonitoring;
    servicemanager::v4::AverageMonitoring  averageMonitoringMsg;

    servicemanager::v4::MonitoringData monitoringData;
    monitoringData.set_ram(1);
    monitoringData.set_cpu(1);
    monitoringData.set_download(1);
    monitoringData.set_upload(1);

    google::protobuf::Timestamp timestamp;
    timestamp.set_seconds(1);
    timestamp.set_nanos(1);
    monitoringData.mutable_timestamp()->CopyFrom(timestamp);

    servicemanager::v4::PartitionUsage partitionUsage;
    partitionUsage.set_name("name");
    partitionUsage.set_used_size(1);
    monitoringData.add_partitions()->CopyFrom(partitionUsage);

    averageMonitoringMsg.mutable_node_monitoring()->CopyFrom(monitoringData);

    servicemanager::v4::InstanceMonitoring instanceMonitoring;
    ::common::v1::InstanceIdent            instanceIdent;
    instanceIdent.set_service_id("service_id");
    instanceIdent.set_subject_id("subject_id");
    instanceIdent.set_instance(1);
    instanceMonitoring.mutable_instance()->CopyFrom(instanceIdent);
    instanceMonitoring.mutable_monitoring_data()->CopyFrom(monitoringData);

    averageMonitoringMsg.add_instances_monitoring()->CopyFrom(instanceMonitoring);

    averageMonitoring.mutable_average_monitoring()->CopyFrom(averageMonitoringMsg);

    return averageMonitoring;
}

servicemanager::v4::SMOutgoingMessages CreateAlert()
{
    servicemanager::v4::SMOutgoingMessages alert;
    servicemanager::v4::Alert              alertMsg;

    google::protobuf::Timestamp timestamp;
    timestamp.set_seconds(1);
    timestamp.set_nanos(1);
    alertMsg.mutable_timestamp()->CopyFrom(timestamp);

    alertMsg.set_tag("tag");

    servicemanager::v4::SystemQuotaAlert systemQuotaAlert;
    systemQuotaAlert.set_parameter("parameter");
    systemQuotaAlert.set_value(1);
    systemQuotaAlert.set_status("status");

    alertMsg.mutable_system_quota_alert()->CopyFrom(systemQuotaAlert);

    alert.mutable_alert()->CopyFrom(alertMsg);

    return alert;
}

servicemanager::v4::SMOutgoingMessages CreateImageContentRequest()
{
    servicemanager::v4::SMOutgoingMessages  imageContentRequest;
    servicemanager::v4::ImageContentRequest imageContentRequestMsg;

    imageContentRequestMsg.set_url("url");
    imageContentRequestMsg.set_request_id(1);
    imageContentRequestMsg.set_content_type("content_type");

    imageContentRequest.mutable_image_content_request()->CopyFrom(imageContentRequestMsg);

    return imageContentRequest;
}

servicemanager::v4::SMOutgoingMessages CreateClockSyncRequest()
{
    servicemanager::v4::SMOutgoingMessages clockSyncRequest;
    servicemanager::v4::ClockSyncRequest   clockSyncRequestMsg;

    clockSyncRequest.mutable_clock_sync_request()->CopyFrom(clockSyncRequestMsg);

    return clockSyncRequest;
}

} // namespace

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class CMClientTest : public ::testing::Test {
public:
    CMClientTest() { mCfg.mCMConfig.mCMServerURL = "localhost:8080"; }

protected:
    void SetUp() override
    {
        tests::utils::InitLog();

        mSMService.emplace(mCfg.mCMConfig.mCMServerURL);

        mCMClient.emplace();
    }

    std::optional<TestSMService> mSMService;
    crypto::CertLoaderItf*       mCertLoader {};
    crypto::x509::ProviderItf*   mCryptoProvider {};
    std::optional<CMClient>      mCMClient;
    config::Config               mCfg;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(CMClientTest, SendOutgoingMsg)
{
    common::iamclient::TLSCredentialsMock certProvider {};
    auto err = mCMClient->Init(mCfg, certProvider, *mCertLoader, *mCryptoProvider, true);
    ASSERT_EQ(err, ErrorEnum::eNone);
    mCMClient->OnConnected();

    // Send a unit config status message
    auto                 outgoingMsg = CreateNodeConfigStatus();
    std::vector<uint8_t> data(outgoingMsg.ByteSizeLong());
    EXPECT_TRUE(outgoingMsg.SerializeToArray(data.data(), data.size()));

    // mCMChannelReverse->Send(data);
    mCMClient->SendMessages(std::move(data));
    mSMService->WaitForResponse();

    EXPECT_TRUE(mSMService->GetOutgoingMsg().has_node_config_status());
    auto unitConfigStatus = mSMService->GetOutgoingMsg().node_config_status();
    EXPECT_EQ(unitConfigStatus.node_id(), "node_id");
    EXPECT_EQ(unitConfigStatus.version(), "version");

    auto errorInfo = unitConfigStatus.error();
    EXPECT_EQ(errorInfo.aos_code(), 1);
    EXPECT_EQ(errorInfo.exit_code(), 1);
    EXPECT_EQ(errorInfo.message(), "message");

    // Send a run instances status message

    outgoingMsg = CreateRunInstancesStatus();
    data.clear();
    data.resize(outgoingMsg.ByteSizeLong());
    EXPECT_TRUE(outgoingMsg.SerializeToArray(data.data(), data.size()));

    mCMClient->SendMessages(data);
    mSMService->WaitForResponse();

    EXPECT_TRUE(mSMService->GetOutgoingMsg().has_run_instances_status());
    auto runInstancesStatus = mSMService->GetOutgoingMsg().run_instances_status();
    EXPECT_EQ(runInstancesStatus.instances_size(), 1);
    auto instanceStatus = runInstancesStatus.instances(0);
    EXPECT_EQ(instanceStatus.service_version(), "service_version");
    EXPECT_EQ(instanceStatus.run_state(), "run_state");
    EXPECT_EQ(instanceStatus.error().aos_code(), 1);
    EXPECT_EQ(instanceStatus.error().exit_code(), 1);
    EXPECT_EQ(instanceStatus.error().message(), "message");
    EXPECT_EQ(instanceStatus.instance().service_id(), "service_id");
    EXPECT_EQ(instanceStatus.instance().subject_id(), "subject_id");
    EXPECT_EQ(instanceStatus.instance().instance(), 1);

    // Send an override env var status message
    outgoingMsg = CreateOverrideEnvVarStatus();
    data.clear();
    data.resize(outgoingMsg.ByteSizeLong());
    EXPECT_TRUE(outgoingMsg.SerializeToArray(data.data(), data.size()));

    mCMClient->SendMessages(data);
    mSMService->WaitForResponse();

    EXPECT_TRUE(mSMService->GetOutgoingMsg().has_override_env_var_status());
    auto overrideEnvVarStatus = mSMService->GetOutgoingMsg().override_env_var_status();
    EXPECT_EQ(overrideEnvVarStatus.env_vars_status_size(), 1);
    auto envVarInstanceStatus = overrideEnvVarStatus.env_vars_status(0);
    EXPECT_EQ(envVarInstanceStatus.instance_filter().service_id(), "service_id");
    EXPECT_EQ(envVarInstanceStatus.instance_filter().subject_id(), "subject_id");
    EXPECT_EQ(envVarInstanceStatus.instance_filter().instance(), 1);
    EXPECT_EQ(envVarInstanceStatus.statuses_size(), 1);
    auto envVarStatus = envVarInstanceStatus.statuses(0);
    EXPECT_EQ(envVarStatus.name(), "name");
    EXPECT_EQ(envVarStatus.error().aos_code(), 1);
    EXPECT_EQ(envVarStatus.error().exit_code(), 1);
    EXPECT_EQ(envVarStatus.error().message(), "message");

    // Send an update instances status message
    outgoingMsg = CreateUpdateInstancesStatus();
    data.clear();
    data.resize(outgoingMsg.ByteSizeLong());
    EXPECT_TRUE(outgoingMsg.SerializeToArray(data.data(), data.size()));

    mCMClient->SendMessages(data);
    mSMService->WaitForResponse();

    EXPECT_TRUE(mSMService->GetOutgoingMsg().has_update_instances_status());
    auto updateInstancesStatus = mSMService->GetOutgoingMsg().update_instances_status();
    EXPECT_EQ(updateInstancesStatus.instances_size(), 1);
    instanceStatus = updateInstancesStatus.instances(0);
    EXPECT_EQ(instanceStatus.service_version(), "service_version");
    EXPECT_EQ(instanceStatus.run_state(), "run_state");
    EXPECT_EQ(instanceStatus.error().aos_code(), 1);
    EXPECT_EQ(instanceStatus.error().exit_code(), 1);
    EXPECT_EQ(instanceStatus.error().message(), "message");
    EXPECT_EQ(instanceStatus.instance().service_id(), "service_id");
    EXPECT_EQ(instanceStatus.instance().subject_id(), "subject_id");
    EXPECT_EQ(instanceStatus.instance().instance(), 1);

    // Send a log data message
    outgoingMsg = CreateLogData();
    data.clear();
    data.resize(outgoingMsg.ByteSizeLong());
    EXPECT_TRUE(outgoingMsg.SerializeToArray(data.data(), data.size()));

    mCMClient->SendMessages(data);
    mSMService->WaitForResponse();

    EXPECT_TRUE(mSMService->GetOutgoingMsg().has_log());
    auto logDataMsg = mSMService->GetOutgoingMsg().log();
    EXPECT_EQ(logDataMsg.log_id(), "log_id");
    EXPECT_EQ(logDataMsg.part_count(), 1);
    EXPECT_EQ(logDataMsg.part(), 1);
    EXPECT_EQ(logDataMsg.data(), "data");
    EXPECT_EQ(logDataMsg.error().aos_code(), 1);
    EXPECT_EQ(logDataMsg.error().exit_code(), 1);
    EXPECT_EQ(logDataMsg.error().message(), "message");

    // Send an instant monitoring message
    outgoingMsg = CreateInstantMonitoring();
    data.clear();
    data.resize(outgoingMsg.ByteSizeLong());
    EXPECT_TRUE(outgoingMsg.SerializeToArray(data.data(), data.size()));

    mCMClient->SendMessages(data);
    mSMService->WaitForResponse();

    EXPECT_TRUE(mSMService->GetOutgoingMsg().has_instant_monitoring());
    auto instantMonitoringMsg = mSMService->GetOutgoingMsg().instant_monitoring();
    EXPECT_EQ(instantMonitoringMsg.node_monitoring().ram(), 1);
    EXPECT_EQ(instantMonitoringMsg.node_monitoring().cpu(), 1);
    EXPECT_EQ(instantMonitoringMsg.node_monitoring().download(), 1);
    EXPECT_EQ(instantMonitoringMsg.node_monitoring().upload(), 1);
    EXPECT_EQ(instantMonitoringMsg.node_monitoring().partitions_size(), 1);
    EXPECT_EQ(instantMonitoringMsg.node_monitoring().partitions(0).name(), "name");
    EXPECT_EQ(instantMonitoringMsg.node_monitoring().partitions(0).used_size(), 1);
    EXPECT_EQ(instantMonitoringMsg.node_monitoring().timestamp().seconds(), 1);
    EXPECT_EQ(instantMonitoringMsg.node_monitoring().timestamp().nanos(), 1);
    EXPECT_EQ(instantMonitoringMsg.instances_monitoring_size(), 1);
    auto instanceMonitoringMsg = instantMonitoringMsg.instances_monitoring(0);
    EXPECT_EQ(instanceMonitoringMsg.instance().service_id(), "service_id");
    EXPECT_EQ(instanceMonitoringMsg.instance().subject_id(), "subject_id");
    EXPECT_EQ(instanceMonitoringMsg.instance().instance(), 1);
    EXPECT_EQ(instanceMonitoringMsg.monitoring_data().ram(), 1);
    EXPECT_EQ(instanceMonitoringMsg.monitoring_data().cpu(), 1);
    EXPECT_EQ(instanceMonitoringMsg.monitoring_data().download(), 1);
    EXPECT_EQ(instanceMonitoringMsg.monitoring_data().upload(), 1);
    EXPECT_EQ(instanceMonitoringMsg.monitoring_data().partitions_size(), 1);
    EXPECT_EQ(instanceMonitoringMsg.monitoring_data().partitions(0).name(), "name");
    EXPECT_EQ(instanceMonitoringMsg.monitoring_data().partitions(0).used_size(), 1);

    // Send an average monitoring message
    outgoingMsg = CreateAverageMonitoring();
    data.clear();
    data.resize(outgoingMsg.ByteSizeLong());
    EXPECT_TRUE(outgoingMsg.SerializeToArray(data.data(), data.size()));

    mCMClient->SendMessages(data);
    mSMService->WaitForResponse();

    EXPECT_TRUE(mSMService->GetOutgoingMsg().has_average_monitoring());
    auto averageMonitoringMsg = mSMService->GetOutgoingMsg().average_monitoring();
    EXPECT_EQ(averageMonitoringMsg.node_monitoring().ram(), 1);
    EXPECT_EQ(averageMonitoringMsg.node_monitoring().cpu(), 1);
    EXPECT_EQ(averageMonitoringMsg.node_monitoring().download(), 1);
    EXPECT_EQ(averageMonitoringMsg.node_monitoring().upload(), 1);
    EXPECT_EQ(averageMonitoringMsg.node_monitoring().partitions_size(), 1);
    EXPECT_EQ(averageMonitoringMsg.node_monitoring().partitions(0).name(), "name");
    EXPECT_EQ(averageMonitoringMsg.node_monitoring().partitions(0).used_size(), 1);
    EXPECT_EQ(averageMonitoringMsg.node_monitoring().timestamp().seconds(), 1);
    EXPECT_EQ(averageMonitoringMsg.node_monitoring().timestamp().nanos(), 1);
    EXPECT_EQ(averageMonitoringMsg.instances_monitoring_size(), 1);
    instanceMonitoringMsg = averageMonitoringMsg.instances_monitoring(0);
    EXPECT_EQ(instanceMonitoringMsg.instance().service_id(), "service_id");
    EXPECT_EQ(instanceMonitoringMsg.instance().subject_id(), "subject_id");
    EXPECT_EQ(instanceMonitoringMsg.instance().instance(), 1);
    EXPECT_EQ(instanceMonitoringMsg.monitoring_data().ram(), 1);
    EXPECT_EQ(instanceMonitoringMsg.monitoring_data().cpu(), 1);
    EXPECT_EQ(instanceMonitoringMsg.monitoring_data().download(), 1);
    EXPECT_EQ(instanceMonitoringMsg.monitoring_data().upload(), 1);
    EXPECT_EQ(instanceMonitoringMsg.monitoring_data().partitions_size(), 1);
    EXPECT_EQ(instanceMonitoringMsg.monitoring_data().partitions(0).name(), "name");
    EXPECT_EQ(instanceMonitoringMsg.monitoring_data().partitions(0).used_size(), 1);

    // Send an alert message
    outgoingMsg = CreateAlert();
    data.clear();
    data.resize(outgoingMsg.ByteSizeLong());

    EXPECT_TRUE(outgoingMsg.SerializeToArray(data.data(), data.size()));

    mCMClient->SendMessages(data);
    mSMService->WaitForResponse();

    EXPECT_TRUE(mSMService->GetOutgoingMsg().has_alert());
    auto alertMsg = mSMService->GetOutgoingMsg().alert();
    EXPECT_EQ(alertMsg.timestamp().seconds(), 1);
    EXPECT_EQ(alertMsg.timestamp().nanos(), 1);
    EXPECT_EQ(alertMsg.tag(), "tag");
    EXPECT_TRUE(alertMsg.has_system_quota_alert());
    auto systemQuotaAlert = alertMsg.system_quota_alert();
    EXPECT_EQ(systemQuotaAlert.parameter(), "parameter");
    EXPECT_EQ(systemQuotaAlert.value(), 1);
    EXPECT_EQ(systemQuotaAlert.status(), "status");

    // Send an image content request message
    outgoingMsg = CreateImageContentRequest();
    data.clear();
    data.resize(outgoingMsg.ByteSizeLong());

    EXPECT_TRUE(outgoingMsg.SerializeToArray(data.data(), data.size()));

    mCMClient->SendMessages(data);
    mSMService->WaitForResponse();

    EXPECT_TRUE(mSMService->GetOutgoingMsg().has_image_content_request());
    auto imageContentRequestMsg = mSMService->GetOutgoingMsg().image_content_request();
    EXPECT_EQ(imageContentRequestMsg.url(), "url");
    EXPECT_EQ(imageContentRequestMsg.request_id(), 1);
    EXPECT_EQ(imageContentRequestMsg.content_type(), "content_type");

    // Send a clock sync request message
    outgoingMsg = CreateClockSyncRequest();
    data.clear();
    data.resize(outgoingMsg.ByteSizeLong());

    EXPECT_TRUE(outgoingMsg.SerializeToArray(data.data(), data.size()));

    mCMClient->SendMessages(data);
    mSMService->WaitForResponse();

    EXPECT_TRUE(mSMService->GetOutgoingMsg().has_clock_sync_request());

    mCMClient->OnDisconnected();
}

TEST_F(CMClientTest, SendIncomingMessages)
{
    common::iamclient::TLSCredentialsMock certProvider {};
    auto err = mCMClient->Init(mCfg, certProvider, *mCertLoader, *mCryptoProvider, true);
    ASSERT_EQ(err, ErrorEnum::eNone);
    mCMClient->OnConnected();

    // Send a get unit config status message
    EXPECT_TRUE(mSMService->SendGetNodeConfigStatus());

    auto [msg, errRecv] = mCMClient->ReceiveMessages();
    ASSERT_EQ(errRecv, ErrorEnum::eNone);

    servicemanager::v4::SMIncomingMessages incomingMsg;

    EXPECT_TRUE(incomingMsg.ParseFromArray(msg.data(), static_cast<int>(msg.size())));
    EXPECT_TRUE(incomingMsg.has_get_node_config_status());

    // Send a check unit config message
    EXPECT_TRUE(mSMService->SendCheckNodeConfig());

    auto res = mCMClient->ReceiveMessages();
    ASSERT_EQ(res.mError, ErrorEnum::eNone);
    EXPECT_TRUE(incomingMsg.ParseFromArray(res.mValue.data(), static_cast<int>(res.mValue.size())));
    EXPECT_TRUE(incomingMsg.has_check_node_config());
    EXPECT_EQ(incomingMsg.check_node_config().node_config(), "unit_config");
    EXPECT_EQ(incomingMsg.check_node_config().version(), "version");

    // Send a set unit config message
    EXPECT_TRUE(mSMService->SendSetUnitConfig());

    res = mCMClient->ReceiveMessages();
    ASSERT_EQ(res.mError, ErrorEnum::eNone);
    EXPECT_TRUE(incomingMsg.ParseFromArray(res.mValue.data(), static_cast<int>(res.mValue.size())));
    EXPECT_TRUE(incomingMsg.has_set_node_config());
    EXPECT_EQ(incomingMsg.set_node_config().node_config(), "unit_config");
    EXPECT_EQ(incomingMsg.set_node_config().version(), "version");

    // Send a run instances message
    EXPECT_TRUE(mSMService->SendRunInstances());

    res = mCMClient->ReceiveMessages();
    ASSERT_EQ(res.mError, ErrorEnum::eNone);

    EXPECT_TRUE(incomingMsg.ParseFromArray(res.mValue.data(), static_cast<int>(res.mValue.size())));
    EXPECT_TRUE(incomingMsg.has_run_instances());
    EXPECT_EQ(incomingMsg.run_instances().services_size(), 1);
    EXPECT_EQ(incomingMsg.run_instances().layers_size(), 1);
    EXPECT_EQ(incomingMsg.run_instances().instances_size(), 1);

    EXPECT_EQ(incomingMsg.run_instances().services(0).service_id(), "service_id");
    EXPECT_EQ(incomingMsg.run_instances().services(0).provider_id(), "provider_id");
    EXPECT_EQ(incomingMsg.run_instances().services(0).version(), "version");
    EXPECT_EQ(incomingMsg.run_instances().services(0).gid(), 1);
    EXPECT_EQ(incomingMsg.run_instances().services(0).url(), "url");
    EXPECT_EQ(incomingMsg.run_instances().services(0).sha256(), "sha256");
    EXPECT_EQ(incomingMsg.run_instances().services(0).size(), 1);

    EXPECT_EQ(incomingMsg.run_instances().layers(0).layer_id(), "layer_id");
    EXPECT_EQ(incomingMsg.run_instances().layers(0).digest(), "digest");
    EXPECT_EQ(incomingMsg.run_instances().layers(0).version(), "version");
    EXPECT_EQ(incomingMsg.run_instances().layers(0).url(), "url");
    EXPECT_EQ(incomingMsg.run_instances().layers(0).sha256(), "sha256");
    EXPECT_EQ(incomingMsg.run_instances().layers(0).size(), 1);

    EXPECT_EQ(incomingMsg.run_instances().instances(0).instance().service_id(), "service_id");
    EXPECT_EQ(incomingMsg.run_instances().instances(0).instance().subject_id(), "subject_id");
    EXPECT_EQ(incomingMsg.run_instances().instances(0).instance().instance(), 1);
    EXPECT_EQ(incomingMsg.run_instances().instances(0).uid(), 1);
    EXPECT_EQ(incomingMsg.run_instances().instances(0).priority(), 1);
    EXPECT_EQ(incomingMsg.run_instances().instances(0).storage_path(), "storage_path");
    EXPECT_EQ(incomingMsg.run_instances().instances(0).state_path(), "state_path");
    EXPECT_EQ(incomingMsg.run_instances().instances(0).network_parameters().network_id(), "network_id");
    EXPECT_EQ(incomingMsg.run_instances().instances(0).network_parameters().subnet(), "subnet");
    EXPECT_EQ(incomingMsg.run_instances().instances(0).network_parameters().ip(), "ip");
    EXPECT_EQ(incomingMsg.run_instances().instances(0).network_parameters().vlan_id(), 1);
    EXPECT_EQ(incomingMsg.run_instances().instances(0).network_parameters().dns_servers_size(), 1);
    EXPECT_EQ(incomingMsg.run_instances().instances(0).network_parameters().dns_servers(0), "dns_servers");
    EXPECT_EQ(incomingMsg.run_instances().instances(0).network_parameters().rules_size(), 1);
    EXPECT_EQ(incomingMsg.run_instances().instances(0).network_parameters().rules(0).dst_ip(), "dst_ip");
    EXPECT_EQ(incomingMsg.run_instances().instances(0).network_parameters().rules(0).dst_port(), "dst_port");
    EXPECT_EQ(incomingMsg.run_instances().instances(0).network_parameters().rules(0).proto(), "proto");
    EXPECT_EQ(incomingMsg.run_instances().instances(0).network_parameters().rules(0).src_ip(), "src_ip");

    // Send a ovveride env vars message
    EXPECT_TRUE(mSMService->SendOverrideEnvVars());

    res = mCMClient->ReceiveMessages();
    ASSERT_EQ(res.mError, ErrorEnum::eNone);

    EXPECT_TRUE(incomingMsg.ParseFromArray(res.mValue.data(), static_cast<int>(res.mValue.size())));
    EXPECT_TRUE(incomingMsg.has_override_env_vars());
    EXPECT_EQ(incomingMsg.override_env_vars().env_vars_size(), 1);
    EXPECT_EQ(incomingMsg.override_env_vars().env_vars(0).instance_filter().service_id(), "service_id");
    EXPECT_EQ(incomingMsg.override_env_vars().env_vars(0).instance_filter().subject_id(), "subject_id");
    EXPECT_EQ(incomingMsg.override_env_vars().env_vars(0).instance_filter().instance(), 1);
    EXPECT_EQ(incomingMsg.override_env_vars().env_vars(0).variables_size(), 1);
    EXPECT_EQ(incomingMsg.override_env_vars().env_vars(0).variables(0).name(), "name");
    EXPECT_EQ(incomingMsg.override_env_vars().env_vars(0).variables(0).value(), "value");

    // Send a system log request message
    EXPECT_TRUE(mSMService->SendSystemLogRequest());

    res = mCMClient->ReceiveMessages();
    ASSERT_EQ(res.mError, ErrorEnum::eNone);

    EXPECT_TRUE(incomingMsg.ParseFromArray(res.mValue.data(), static_cast<int>(res.mValue.size())));
    EXPECT_TRUE(incomingMsg.has_system_log_request());
    EXPECT_EQ(incomingMsg.system_log_request().log_id(), "log_id");
    EXPECT_EQ(incomingMsg.system_log_request().from().seconds(), 1);
    EXPECT_EQ(incomingMsg.system_log_request().from().nanos(), 1);
    EXPECT_EQ(incomingMsg.system_log_request().till().seconds(), 1);
    EXPECT_EQ(incomingMsg.system_log_request().till().nanos(), 1);

    // Send an instance log request message
    EXPECT_TRUE(mSMService->SendInstanceLogRequest());

    res = mCMClient->ReceiveMessages();
    ASSERT_EQ(res.mError, ErrorEnum::eNone);

    EXPECT_TRUE(incomingMsg.ParseFromArray(res.mValue.data(), static_cast<int>(res.mValue.size())));
    EXPECT_TRUE(incomingMsg.has_instance_log_request());
    EXPECT_EQ(incomingMsg.instance_log_request().log_id(), "log_id");
    EXPECT_EQ(incomingMsg.instance_log_request().instance_filter().service_id(), "service_id");
    EXPECT_EQ(incomingMsg.instance_log_request().instance_filter().subject_id(), "subject_id");
    EXPECT_EQ(incomingMsg.instance_log_request().instance_filter().instance(), 1);
    EXPECT_EQ(incomingMsg.instance_log_request().from().seconds(), 1);
    EXPECT_EQ(incomingMsg.instance_log_request().from().nanos(), 1);
    EXPECT_EQ(incomingMsg.instance_log_request().till().seconds(), 1);
    EXPECT_EQ(incomingMsg.instance_log_request().till().nanos(), 1);

    // Send a instance crash log request message
    EXPECT_TRUE(mSMService->SendInstanceCrashLogRequest());

    res = mCMClient->ReceiveMessages();
    ASSERT_EQ(res.mError, ErrorEnum::eNone);

    EXPECT_TRUE(incomingMsg.ParseFromArray(res.mValue.data(), static_cast<int>(res.mValue.size())));
    EXPECT_TRUE(incomingMsg.has_instance_crash_log_request());
    EXPECT_EQ(incomingMsg.instance_crash_log_request().log_id(), "log_id");
    EXPECT_EQ(incomingMsg.instance_crash_log_request().instance_filter().service_id(), "service_id");
    EXPECT_EQ(incomingMsg.instance_crash_log_request().instance_filter().subject_id(), "subject_id");
    EXPECT_EQ(incomingMsg.instance_crash_log_request().instance_filter().instance(), 1);
    EXPECT_EQ(incomingMsg.instance_crash_log_request().from().seconds(), 1);
    EXPECT_EQ(incomingMsg.instance_crash_log_request().from().nanos(), 1);
    EXPECT_EQ(incomingMsg.instance_crash_log_request().till().seconds(), 1);
    EXPECT_EQ(incomingMsg.instance_crash_log_request().till().nanos(), 1);

    // Send a get average monitoring message
    EXPECT_TRUE(mSMService->SendGetAverageMonitoring());

    res = mCMClient->ReceiveMessages();
    ASSERT_EQ(res.mError, ErrorEnum::eNone);

    EXPECT_TRUE(incomingMsg.ParseFromArray(res.mValue.data(), static_cast<int>(res.mValue.size())));
    EXPECT_TRUE(incomingMsg.has_get_average_monitoring());

    // Send a connection status message
    EXPECT_TRUE(mSMService->SendConnectionStatus());

    res = mCMClient->ReceiveMessages();
    ASSERT_EQ(res.mError, ErrorEnum::eNone);

    EXPECT_TRUE(incomingMsg.ParseFromArray(res.mValue.data(), static_cast<int>(res.mValue.size())));
    EXPECT_TRUE(incomingMsg.has_connection_status());
    EXPECT_EQ(incomingMsg.connection_status().cloud_status(), servicemanager::v4::ConnectionEnum::CONNECTED);

    // Send a image content info message
    EXPECT_TRUE(mSMService->SendImageContentInfo());

    res = mCMClient->ReceiveMessages();
    ASSERT_EQ(res.mError, ErrorEnum::eNone);

    EXPECT_TRUE(incomingMsg.ParseFromArray(res.mValue.data(), static_cast<int>(res.mValue.size())));
    EXPECT_TRUE(incomingMsg.has_image_content_info());
    EXPECT_EQ(incomingMsg.image_content_info().request_id(), 1);
    EXPECT_EQ(incomingMsg.image_content_info().image_files_size(), 1);
    EXPECT_EQ(incomingMsg.image_content_info().image_files(0).relative_path(), "relative_path");
    EXPECT_EQ(incomingMsg.image_content_info().image_files(0).sha256(), "sha256");
    EXPECT_EQ(incomingMsg.image_content_info().image_files(0).size(), 1);
    EXPECT_EQ(incomingMsg.image_content_info().error().aos_code(), 1);
    EXPECT_EQ(incomingMsg.image_content_info().error().exit_code(), 1);
    EXPECT_EQ(incomingMsg.image_content_info().error().message(), "message");

    // Send a image content message
    EXPECT_TRUE(mSMService->SendImageContent());

    res = mCMClient->ReceiveMessages();
    ASSERT_EQ(res.mError, ErrorEnum::eNone);

    EXPECT_TRUE(incomingMsg.ParseFromArray(res.mValue.data(), static_cast<int>(res.mValue.size())));
    EXPECT_TRUE(incomingMsg.has_image_content());
    EXPECT_EQ(incomingMsg.image_content().request_id(), 1);
    EXPECT_EQ(incomingMsg.image_content().relative_path(), "relative_path");
    EXPECT_EQ(incomingMsg.image_content().parts_count(), 1);
    EXPECT_EQ(incomingMsg.image_content().part(), 1);
    EXPECT_EQ(incomingMsg.image_content().data(), "data");

    // Send a update networks message
    EXPECT_TRUE(mSMService->SendUpdateNetworks());

    res = mCMClient->ReceiveMessages();
    ASSERT_EQ(res.mError, ErrorEnum::eNone);

    EXPECT_TRUE(incomingMsg.ParseFromArray(res.mValue.data(), static_cast<int>(res.mValue.size())));
    EXPECT_TRUE(incomingMsg.has_update_networks());
    EXPECT_EQ(incomingMsg.update_networks().networks_size(), 1);
    EXPECT_EQ(incomingMsg.update_networks().networks(0).network_id(), "network_id");
    EXPECT_EQ(incomingMsg.update_networks().networks(0).subnet(), "subnet");
    EXPECT_EQ(incomingMsg.update_networks().networks(0).ip(), "ip");
    EXPECT_EQ(incomingMsg.update_networks().networks(0).vlan_id(), 1);
    EXPECT_EQ(incomingMsg.update_networks().networks(0).dns_servers_size(), 1);
    EXPECT_EQ(incomingMsg.update_networks().networks(0).dns_servers(0), "dns_servers");
    EXPECT_EQ(incomingMsg.update_networks().networks(0).rules_size(), 1);
    EXPECT_EQ(incomingMsg.update_networks().networks(0).rules(0).dst_ip(), "dst_ip");
    EXPECT_EQ(incomingMsg.update_networks().networks(0).rules(0).dst_port(), "dst_port");
    EXPECT_EQ(incomingMsg.update_networks().networks(0).rules(0).proto(), "proto");
    EXPECT_EQ(incomingMsg.update_networks().networks(0).rules(0).src_ip(), "src_ip");

    // Send a clock sync message
    EXPECT_TRUE(mSMService->SendClockSync());

    res = mCMClient->ReceiveMessages();
    ASSERT_EQ(res.mError, ErrorEnum::eNone);

    EXPECT_TRUE(incomingMsg.ParseFromArray(res.mValue.data(), static_cast<int>(res.mValue.size())));
    EXPECT_TRUE(incomingMsg.has_clock_sync());
    EXPECT_EQ(incomingMsg.clock_sync().current_time().seconds(), 1);
    EXPECT_EQ(incomingMsg.clock_sync().current_time().nanos(), 1);

    mCMClient->OnDisconnected();
}

TEST_F(CMClientTest, CertChanged)
{
    common::iamclient::TLSCredentialsMock certProvider {};
    EXPECT_CALL(certProvider, GetMTLSClientCredentials(_))
        .Times(2)
        .WillRepeatedly(testing::Return(std::shared_ptr<grpc::ChannelCredentials>(grpc::InsecureChannelCredentials())));

    auto err = mCMClient->Init(mCfg, certProvider, *mCertLoader, *mCryptoProvider, false);
    ASSERT_EQ(err, ErrorEnum::eNone);

    mCMClient->OnConnected();
    EXPECT_TRUE(mSMService->WaitForConnection());

    mCMClient->OnCertChanged(CertInfo {});

    EXPECT_TRUE(mSMService->WaitForDisconnection());
    EXPECT_TRUE(mSMService->WaitForConnection());

    mCMClient->OnDisconnected();
}

} // namespace aos::mp::cmclient
