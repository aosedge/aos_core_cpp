/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <core/common/tests/utils/log.hpp>

#include <cm/smcontroller/smcontroller.hpp>

#include "stubs/alertsreceiverstub.hpp"
#include "stubs/certloaderstub.hpp"
#include "stubs/certproviderstub.hpp"
#include "stubs/cloudconnectionstub.hpp"
#include "stubs/instancestatusreceiverstub.hpp"
#include "stubs/iteminfoproviderstub.hpp"
#include "stubs/launchersenderstub.hpp"
#include "stubs/monitoringreceiverstub.hpp"
#include "stubs/networkproviderstub.hpp"
#include "stubs/smclientstub.hpp"
#include "stubs/smcontrollersenderstub.hpp"
#include "stubs/sminforeceiverstub.hpp"
#include "stubs/x509providerstub.hpp"

using namespace testing;

namespace aos::cm::smcontroller {

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class SMControllerTest : public Test {
protected:
    static constexpr auto cMainNodeID      = "main";
    static constexpr auto cSecondaryNodeID = "secondary";

    void SetUp() override
    {
        tests::utils::InitLog();

        mConfig.mCMServerURL = "localhost:8094";

        auto err = mSMController.Init(mConfig, mCloudConnection, mCertProvider, mCertLoader, mX509Provider,
            mItemInfoProvider, mAlertsReceiver, mSMControllerSender, mLauncherSender, mMonitoringReceiver,
            mInstanceStatusReceiver, mSMInfoReceiver, mNetworkProvider, true);
        ASSERT_TRUE(err.IsNone()) << err.Message();

        err = mSMController.Start();
        ASSERT_TRUE(err.IsNone()) << err.Message();
    }

    void TearDown() override
    {
        auto err = mSMController.Stop();

        EXPECT_TRUE(err.IsNone()) << err.Message();
    }

    InstanceIdent CreateInstanceIdent(const char* serviceID, const char* subjectID, uint64_t instance)
    {
        InstanceIdent ident;

        ident.mItemID    = serviceID;
        ident.mSubjectID = subjectID;
        ident.mInstance  = instance;

        return ident;
    }

    EnvVarsInstanceInfo CreateEnvVarsInstanceInfo(
        const char* serviceID, const char* subjectID, uint64_t instance, const char* varName, const char* varValue)
    {
        EnvVarsInstanceInfo envVarsInstance;

        envVarsInstance.mItemID.SetValue(serviceID);
        envVarsInstance.mSubjectID.SetValue(subjectID);
        envVarsInstance.mInstance.SetValue(instance);

        EnvVarInfo var;

        var.mName  = varName;
        var.mValue = varValue;
        envVarsInstance.mVariables.PushBack(var);

        return envVarsInstance;
    }

    SMController mSMController;
    Config       mConfig;

    CloudConnectionStub                  mCloudConnection;
    iamclient::CertProviderStub          mCertProvider;
    crypto::CertLoaderStub               mCertLoader;
    crypto::x509::ProviderStub           mX509Provider;
    ItemInfoProviderStub                 mItemInfoProvider;
    alerts::ReceiverStub                 mAlertsReceiver;
    SenderStub                           mSMControllerSender;
    launcher::SenderStub                 mLauncherSender;
    monitoring::ReceiverStub             mMonitoringReceiver;
    launcher::InstanceStatusReceiverStub mInstanceStatusReceiver;
    nodeinfoprovider::SMInfoReceiverStub mSMInfoReceiver;
    NetworkProviderStub                  mNetworkProvider;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(SMControllerTest, SMClientConnected)
{
    // 1) Start client
    SMClientStub client;

    auto err = client.Init(cMainNodeID);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    err = client.Start(mConfig.mCMServerURL);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    // 2) Wait for connection
    err = mSMInfoReceiver.WaitConnect(cMainNodeID);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    EXPECT_TRUE(mSMInfoReceiver.IsNodeConnected(cMainNodeID));

    // 3) Wait for SM info
    err = mSMInfoReceiver.WaitSMInfo(cMainNodeID);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    ASSERT_TRUE(mSMInfoReceiver.HasSMInfo(cMainNodeID));

    auto smInfo = mSMInfoReceiver.GetSMInfo(cMainNodeID);

    EXPECT_EQ(smInfo.mNodeID, String(cMainNodeID));

    // 4) Stop client
    err = client.Stop();
    ASSERT_TRUE(err.IsNone()) << err.Message();

    // 5) Wait for disconnect
    err = mSMInfoReceiver.WaitDisconnect(cMainNodeID);
    EXPECT_TRUE(err.IsNone()) << err.Message();

    EXPECT_FALSE(mSMInfoReceiver.IsNodeConnected(cMainNodeID));
}

TEST_F(SMControllerTest, CheckNodeConfig)
{
    // 1) Start client
    SMClientStub client;

    auto err = client.Init(cMainNodeID);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    err = client.Start(mConfig.mCMServerURL);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    // 2) Wait for SM info
    err = mSMInfoReceiver.WaitSMInfo(cMainNodeID);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    // 3) Check node config
    NodeConfig nodeConfig;

    nodeConfig.mNodeID   = cMainNodeID;
    nodeConfig.mNodeType = "main";
    nodeConfig.mVersion  = "1.0.0";
    // check OK
    err = mSMController.CheckNodeConfig(cMainNodeID, nodeConfig);
    EXPECT_TRUE(err.IsNone()) << err.Message();
    // check Not Found
    err = mSMController.CheckNodeConfig(cSecondaryNodeID, nodeConfig);
    EXPECT_FALSE(err.IsNone()) << err.Message();

    // 4) Stop client
    err = client.Stop();
    ASSERT_TRUE(err.IsNone()) << err.Message();

    // 5) Wait for disconnect
    err = mSMInfoReceiver.WaitDisconnect(cMainNodeID);
    EXPECT_TRUE(err.IsNone()) << err.Message();
}

TEST_F(SMControllerTest, UpdateNodeConfig)
{
    // 1) Start client
    SMClientStub client;

    auto err = client.Init(cMainNodeID);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    err = client.Start(mConfig.mCMServerURL);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    // 2) Wait for SM info
    err = mSMInfoReceiver.WaitSMInfo(cMainNodeID);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    // 3) Update node config
    NodeConfig nodeConfig;

    nodeConfig.mNodeID   = cMainNodeID;
    nodeConfig.mNodeType = "main";
    nodeConfig.mVersion  = "1.0.0";
    // update OK
    err = mSMController.UpdateNodeConfig(cMainNodeID, nodeConfig);
    EXPECT_TRUE(err.IsNone()) << err.Message();
    // update Not Found
    err = mSMController.UpdateNodeConfig(cSecondaryNodeID, nodeConfig);
    EXPECT_FALSE(err.IsNone()) << err.Message();

    // 4) Stop client
    err = client.Stop();
    ASSERT_TRUE(err.IsNone()) << err.Message();

    // 5) Wait for disconnect
    err = mSMInfoReceiver.WaitDisconnect(cMainNodeID);
    EXPECT_TRUE(err.IsNone()) << err.Message();
}

TEST_F(SMControllerTest, GetNodeConfigStatus)
{
    // 1) Start client
    SMClientStub client;

    auto err = client.Init(cMainNodeID);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    err = client.Start(mConfig.mCMServerURL);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    // 2) Wait for SM info
    err = mSMInfoReceiver.WaitSMInfo(cMainNodeID);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    ASSERT_TRUE(mSMInfoReceiver.HasSMInfo(cMainNodeID));

    // 3) Get node config status
    NodeConfigStatus status;

    // get OK
    err = mSMController.GetNodeConfigStatus(cMainNodeID, status);
    EXPECT_TRUE(err.IsNone()) << err.Message();

    // get Not Found
    err = mSMController.GetNodeConfigStatus(cSecondaryNodeID, status);
    EXPECT_FALSE(err.IsNone()) << err.Message();

    // 4) Stop client
    err = client.Stop();
    ASSERT_TRUE(err.IsNone()) << err.Message();

    // 5) Wait for disconnect
    err = mSMInfoReceiver.WaitDisconnect(cMainNodeID);
    EXPECT_TRUE(err.IsNone()) << err.Message();
}

TEST_F(SMControllerTest, RequestLog)
{
    // 1) Start client
    SMClientStub client;

    auto err = client.Init(cMainNodeID);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    err = client.Start(mConfig.mCMServerURL);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    // 2) Wait for SM info
    err = mSMInfoReceiver.WaitSMInfo(cMainNodeID);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    ASSERT_TRUE(mSMInfoReceiver.HasSMInfo(cMainNodeID));

    // 3.1) Request system log
    RequestLog systemLog;

    systemLog.mCorrelationID = "system-log-id";
    systemLog.mLogType       = LogTypeEnum::eSystemLog;
    systemLog.mFilter.mNodes.EmplaceBack(cMainNodeID);

    err = mSMController.RequestLog(systemLog);
    EXPECT_TRUE(err.IsNone()) << err.Message();

    // Wait for both parts
    err = mSMControllerSender.WaitLog("system-log-id", 0);
    EXPECT_TRUE(err.IsNone()) << err.Message();

    err = mSMControllerSender.WaitLog("system-log-id", 1);
    EXPECT_TRUE(err.IsNone()) << err.Message();

    // 3.2) Request instance log
    RequestLog instanceLog;

    instanceLog.mCorrelationID = "instance-log-id";
    instanceLog.mLogType       = LogTypeEnum::eInstanceLog;
    instanceLog.mFilter.mNodes.EmplaceBack(cMainNodeID);

    err = mSMController.RequestLog(instanceLog);
    EXPECT_TRUE(err.IsNone()) << err.Message();

    // Wait for both parts
    err = mSMControllerSender.WaitLog("instance-log-id", 0);
    EXPECT_TRUE(err.IsNone()) << err.Message();

    err = mSMControllerSender.WaitLog("instance-log-id", 1);
    EXPECT_TRUE(err.IsNone()) << err.Message();

    // 3.3) Request crash log
    RequestLog crashLog;

    crashLog.mCorrelationID = "crash-log-id";
    crashLog.mLogType       = LogTypeEnum::eCrashLog;
    crashLog.mFilter.mNodes.EmplaceBack(cMainNodeID);

    err = mSMController.RequestLog(crashLog);
    EXPECT_TRUE(err.IsNone()) << err.Message();

    // Wait for both parts
    err = mSMControllerSender.WaitLog("crash-log-id", 0);
    EXPECT_TRUE(err.IsNone()) << err.Message();

    err = mSMControllerSender.WaitLog("crash-log-id", 1);
    EXPECT_TRUE(err.IsNone()) << err.Message();

    // 3.4) Request log for non-existent node
    RequestLog notFoundLog;

    notFoundLog.mCorrelationID = "not-found-log-id";
    notFoundLog.mLogType       = LogTypeEnum::eSystemLog;
    notFoundLog.mFilter.mNodes.EmplaceBack(cSecondaryNodeID);

    err = mSMController.RequestLog(notFoundLog);
    EXPECT_FALSE(err.IsNone()) << err.Message();

    // 4) Stop client
    err = client.Stop();
    ASSERT_TRUE(err.IsNone()) << err.Message();

    // 5) Wait for disconnect
    err = mSMInfoReceiver.WaitDisconnect(cMainNodeID);
    EXPECT_TRUE(err.IsNone()) << err.Message();
}

TEST_F(SMControllerTest, GetNodeNetworkParams)
{
    NetworkParams expectedParams;
    expectedParams.mSubnet = "192.168.1.0/24";
    expectedParams.mIP     = "192.168.1.1";
    expectedParams.mVlanID = 100;
    mNetworkProvider.SetNodeNetworkParams(expectedParams);

    SMClientStub client;

    auto err = client.Init(cMainNodeID);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    err = client.Start(mConfig.mCMServerURL);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    err = mSMInfoReceiver.WaitSMInfo(cMainNodeID);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    servicemanager::v5::GetNodeNetworkParamsResponse response;

    err = client.GetNodeNetworkParams("network1", cMainNodeID, response);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    EXPECT_EQ(response.subnet(), "192.168.1.0/24");
    EXPECT_EQ(response.ip(), "192.168.1.1");
    EXPECT_EQ(response.vlan_id(), 100);
    EXPECT_FALSE(response.has_error());

    EXPECT_EQ(mNetworkProvider.mLastNetworkID, String("network1"));
    EXPECT_EQ(mNetworkProvider.mLastNodeID, String(cMainNodeID));

    mNetworkProvider.SetError(Error(ErrorEnum::eNotFound, "network not found"));

    err = client.GetNodeNetworkParams("network2", cMainNodeID, response);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    EXPECT_TRUE(response.has_error());

    err = client.Stop();
    ASSERT_TRUE(err.IsNone()) << err.Message();

    err = mSMInfoReceiver.WaitDisconnect(cMainNodeID);
    EXPECT_TRUE(err.IsNone()) << err.Message();
}

TEST_F(SMControllerTest, AllocateInstanceNetwork)
{
    InstanceNetworkAllocation expectedParams;
    expectedParams.mSubnet = "192.168.1.0/24";
    expectedParams.mIP     = "192.168.1.10";
    expectedParams.mDNSServers.PushBack("8.8.8.8");
    mNetworkProvider.SetInstanceNetworkParams(expectedParams);

    SMClientStub client;

    auto err = client.Init(cMainNodeID);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    err = client.Start(mConfig.mCMServerURL);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    err = mSMInfoReceiver.WaitSMInfo(cMainNodeID);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    auto instanceIdent = CreateInstanceIdent("service1", "subject1", 0);

    servicemanager::v5::AllocateInstanceNetworkResponse response;

    err = client.AllocateInstanceNetwork(instanceIdent, "network1", cMainNodeID, response);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    EXPECT_EQ(response.subnet(), "192.168.1.0/24");
    EXPECT_EQ(response.ip(), "192.168.1.10");
    ASSERT_EQ(response.dns_servers_size(), 1);
    EXPECT_EQ(response.dns_servers(0), "8.8.8.8");
    EXPECT_FALSE(response.has_error());

    EXPECT_EQ(mNetworkProvider.mLastNetworkID, String("network1"));
    EXPECT_EQ(mNetworkProvider.mLastNodeID, String(cMainNodeID));
    EXPECT_EQ(mNetworkProvider.mLastInstance.mItemID, String("service1"));

    mNetworkProvider.SetError(Error(ErrorEnum::eFailed, "allocation failed"));

    err = client.AllocateInstanceNetwork(instanceIdent, "network1", cMainNodeID, response);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    EXPECT_TRUE(response.has_error());

    err = client.Stop();
    ASSERT_TRUE(err.IsNone()) << err.Message();

    err = mSMInfoReceiver.WaitDisconnect(cMainNodeID);
    EXPECT_TRUE(err.IsNone()) << err.Message();
}

TEST_F(SMControllerTest, ReleaseInstanceNetwork)
{
    SMClientStub client;

    auto err = client.Init(cMainNodeID);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    err = client.Start(mConfig.mCMServerURL);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    err = mSMInfoReceiver.WaitSMInfo(cMainNodeID);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    auto instanceIdent = CreateInstanceIdent("service1", "subject1", 0);

    servicemanager::v5::ReleaseInstanceNetworkResponse response;

    err = client.ReleaseInstanceNetwork(instanceIdent, cMainNodeID, response);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    EXPECT_FALSE(response.has_error());

    EXPECT_EQ(mNetworkProvider.mLastNodeID, String(cMainNodeID));
    EXPECT_EQ(mNetworkProvider.mLastInstance.mItemID, String("service1"));

    mNetworkProvider.SetError(Error(ErrorEnum::eFailed, "release failed"));

    err = client.ReleaseInstanceNetwork(instanceIdent, cMainNodeID, response);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    EXPECT_TRUE(response.has_error());

    err = client.Stop();
    ASSERT_TRUE(err.IsNone()) << err.Message();

    err = mSMInfoReceiver.WaitDisconnect(cMainNodeID);
    EXPECT_TRUE(err.IsNone()) << err.Message();
}

TEST_F(SMControllerTest, ReleaseNodeNetwork)
{
    SMClientStub client;

    auto err = client.Init(cMainNodeID);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    err = client.Start(mConfig.mCMServerURL);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    err = mSMInfoReceiver.WaitSMInfo(cMainNodeID);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    servicemanager::v5::ReleaseNodeNetworkResponse response;

    err = client.ReleaseNodeNetwork("network1", cMainNodeID, response);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    EXPECT_FALSE(response.has_error());

    EXPECT_EQ(mNetworkProvider.mLastNetworkID, String("network1"));
    EXPECT_EQ(mNetworkProvider.mLastNodeID, String(cMainNodeID));

    mNetworkProvider.SetError(Error(ErrorEnum::eFailed, "release failed"));

    err = client.ReleaseNodeNetwork("network1", cMainNodeID, response);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    EXPECT_TRUE(response.has_error());

    err = client.Stop();
    ASSERT_TRUE(err.IsNone()) << err.Message();

    err = mSMInfoReceiver.WaitDisconnect(cMainNodeID);
    EXPECT_TRUE(err.IsNone()) << err.Message();
}

TEST_F(SMControllerTest, UpdateInstances)
{
    // 1) Start client
    SMClientStub client;

    auto err = client.Init(cMainNodeID);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    err = client.Start(mConfig.mCMServerURL);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    // 2) Wait for SM info
    err = mSMInfoReceiver.WaitSMInfo(cMainNodeID);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    ASSERT_TRUE(mSMInfoReceiver.HasSMInfo(cMainNodeID));

    // 3) Update instances
    StaticArray<InstanceInfo, 2> stopInstances;
    InstanceInfo                 stopInstance1;

    static_cast<InstanceIdent&>(stopInstance1) = CreateInstanceIdent("service1", "subject1", 0);
    stopInstances.PushBack(stopInstance1);

    StaticArray<InstanceInfo, 2> startInstances;
    InstanceInfo                 startInstance1;

    static_cast<InstanceIdent&>(startInstance1) = CreateInstanceIdent("service2", "subject2", 1);

    startInstance1.mManifestDigest = "image2";
    startInstances.PushBack(startInstance1);

    // update OK
    err = mSMController.UpdateInstances(cMainNodeID, stopInstances, startInstances);
    EXPECT_TRUE(err.IsNone()) << err.Message();

    // Wait for start instance status
    err = mInstanceStatusReceiver.WaitInstanceStatus(cMainNodeID, startInstance1);
    EXPECT_TRUE(err.IsNone()) << err.Message();

    // update Not Found
    err = mSMController.UpdateInstances(cSecondaryNodeID, stopInstances, startInstances);
    EXPECT_FALSE(err.IsNone()) << err.Message();

    // 4) Stop client
    err = client.Stop();
    ASSERT_TRUE(err.IsNone()) << err.Message();

    // 5) Wait for disconnect
    err = mSMInfoReceiver.WaitDisconnect(cMainNodeID);
    EXPECT_TRUE(err.IsNone()) << err.Message();
}

TEST_F(SMControllerTest, InstanceStatusesReceived)
{
    // 1) Start client
    SMClientStub client;

    auto err = client.Init(cMainNodeID);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    err = client.Start(mConfig.mCMServerURL);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    // 2) Wait for SM info
    err = mSMInfoReceiver.WaitSMInfo(cMainNodeID);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    // 3) Send instance status
    auto instanceIdent = CreateInstanceIdent("service1", "subject1", 0);

    err = client.SendUpdateInstancesStatus(instanceIdent, InstanceStateEnum::eActivating);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    // 4) Wait for instance status
    err = mInstanceStatusReceiver.WaitInstanceStatus(cMainNodeID, instanceIdent);
    EXPECT_TRUE(err.IsNone()) << err.Message();

    // 5) Stop client
    err = client.Stop();
    ASSERT_TRUE(err.IsNone()) << err.Message();

    // 6) Wait for disconnect
    err = mSMInfoReceiver.WaitDisconnect(cMainNodeID);
    EXPECT_TRUE(err.IsNone()) << err.Message();
}

TEST_F(SMControllerTest, GetAverageMonitoring)
{
    // 1) Start client
    SMClientStub client;

    auto err = client.Init(cMainNodeID);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    err = client.Start(mConfig.mCMServerURL);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    // 2) Wait for SM info
    err = mSMInfoReceiver.WaitSMInfo(cMainNodeID);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    // 3) Get average monitoring
    aos::monitoring::NodeMonitoringData monitoring;

    // get OK
    err = mSMController.GetAverageMonitoring(cMainNodeID, monitoring);
    EXPECT_TRUE(err.IsNone()) << err.Message();

    EXPECT_EQ(monitoring.mNodeID, String(cMainNodeID));
    EXPECT_EQ(monitoring.mMonitoringData.mCPU, 50);
    EXPECT_EQ(monitoring.mMonitoringData.mRAM, 1024);

    // get Not Found
    err = mSMController.GetAverageMonitoring(cSecondaryNodeID, monitoring);
    EXPECT_FALSE(err.IsNone()) << err.Message();

    // 4) Stop client
    err = client.Stop();
    ASSERT_TRUE(err.IsNone()) << err.Message();

    // 5) Wait for disconnect
    err = mSMInfoReceiver.WaitDisconnect(cMainNodeID);
    EXPECT_TRUE(err.IsNone()) << err.Message();
}

TEST_F(SMControllerTest, InstantMonitoringReceived)
{
    // 1) Start client
    SMClientStub client;

    auto err = client.Init(cMainNodeID);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    err = client.Start(mConfig.mCMServerURL);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    // 2) Wait for SM info
    err = mSMInfoReceiver.WaitSMInfo(cMainNodeID);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    // 3) Send instant monitoring
    auto instanceIdent = CreateInstanceIdent("service1", "subject1", 0);

    err = client.SendInstantMonitoring(instanceIdent);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    // 4) Wait for monitoring data
    err = mMonitoringReceiver.WaitMonitoringData(cMainNodeID, instanceIdent);
    EXPECT_TRUE(err.IsNone()) << err.Message();

    auto instMonitoring = mMonitoringReceiver.GetInstanceMonitoringData(cMainNodeID, instanceIdent);

    EXPECT_EQ(instMonitoring.mMonitoringData.mCPU, 80);
    EXPECT_EQ(instMonitoring.mMonitoringData.mRAM, 1536);

    // 5) Stop client
    err = client.Stop();
    ASSERT_TRUE(err.IsNone()) << err.Message();

    // 6) Wait for disconnect
    err = mSMInfoReceiver.WaitDisconnect(cMainNodeID);
    EXPECT_TRUE(err.IsNone()) << err.Message();
}

TEST_F(SMControllerTest, CloudConnectedReceived)
{
    // 1) Start client
    SMClientStub client;

    auto err = client.Init(cMainNodeID);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    err = client.Start(mConfig.mCMServerURL);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    // 2) Wait for SM info
    err = mSMInfoReceiver.WaitSMInfo(cMainNodeID);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    // 3) Wait for cloud connection
    err = client.WaitCloudConnection();
    ASSERT_TRUE(err.IsNone()) << err.Message();
    EXPECT_FALSE(client.IsCloudConnected());

    client.ResetCloudStatus();

    // 4) Trigger cloud connection event
    mCloudConnection.TriggerConnect();

    // 5) Wait for cloud connection
    err = client.WaitCloudConnection();
    ASSERT_TRUE(err.IsNone()) << err.Message();
    EXPECT_TRUE(client.IsCloudConnected());

    // 6) Stop client
    err = client.Stop();
    ASSERT_TRUE(err.IsNone()) << err.Message();

    // 7) Wait for disconnect
    err = mSMInfoReceiver.WaitDisconnect(cMainNodeID);
    EXPECT_TRUE(err.IsNone()) << err.Message();
}

TEST_F(SMControllerTest, CloudDisconnectedReceived)
{
    // 1) Start client
    SMClientStub client;

    auto err = client.Init(cMainNodeID);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    err = client.Start(mConfig.mCMServerURL);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    // 2) Wait for SM info
    err = mSMInfoReceiver.WaitSMInfo(cMainNodeID);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    // 3) Trigger cloud connection event
    mCloudConnection.TriggerDisconnect();

    // 4) Wait for cloud connection
    err = client.WaitCloudConnection();
    ASSERT_TRUE(err.IsNone()) << err.Message();
    EXPECT_FALSE(client.IsCloudConnected());

    // 5) Stop client
    err = client.Stop();
    ASSERT_TRUE(err.IsNone()) << err.Message();

    // 6) Wait for disconnect
    err = mSMInfoReceiver.WaitDisconnect(cMainNodeID);
    EXPECT_TRUE(err.IsNone()) << err.Message();
}

TEST_F(SMControllerTest, AlertReceived)
{
    // 1) Start client
    SMClientStub client;

    auto err = client.Init(cMainNodeID);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    err = client.Start(mConfig.mCMServerURL);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    // 2) Wait for SM info
    err = mSMInfoReceiver.WaitSMInfo(cMainNodeID);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    // 3) Send system alert
    const std::string alertMessage = "Test system alert";

    err = client.SendSystemAlert(alertMessage);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    // 4) Wait for alert
    err = mAlertsReceiver.WaitAlert(cMainNodeID);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    auto latestAlert = mAlertsReceiver.GetLatestAlert(cMainNodeID);
    EXPECT_EQ(latestAlert.mMessage.CStr(), alertMessage);
    EXPECT_EQ(latestAlert.mNodeID.CStr(), std::string(cMainNodeID));

    // 5) Stop client
    err = client.Stop();
    ASSERT_TRUE(err.IsNone()) << err.Message();

    // 6) Wait for disconnect
    err = mSMInfoReceiver.WaitDisconnect(cMainNodeID);
    EXPECT_TRUE(err.IsNone()) << err.Message();
}

TEST_F(SMControllerTest, OnCertChanged)
{
    // 1) Start client
    SMClientStub client;

    auto err = client.Init(cMainNodeID);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    err = client.Start(mConfig.mCMServerURL);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    // 2) Wait for SM info
    err = mSMInfoReceiver.WaitSMInfo(cMainNodeID);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    // 3) Verify listener is registered
    auto* listener = mCertProvider.GetListener();
    ASSERT_NE(listener, nullptr);

    // 4) Trigger certificate change
    CertInfo certInfo;
    certInfo.mCertType = "online";
    certInfo.mCertURL  = "file:///path/to/cert.pem";
    certInfo.mKeyURL   = "file:///path/to/key.pem";

    listener->OnCertChanged(certInfo);

    // 5) Wait for reconnection - old client should disconnect
    err = mSMInfoReceiver.WaitDisconnect(cMainNodeID, std::chrono::seconds(11));
    ASSERT_TRUE(err.IsNone()) << err.Message();

    // 6) Reconnect client after server restart
    err = client.Stop();
    ASSERT_TRUE(err.IsNone()) << err.Message();

    err = client.Start(mConfig.mCMServerURL);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    // 7) Wait for SM info after reconnection
    err = mSMInfoReceiver.WaitSMInfo(cMainNodeID);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    // 8) Stop client
    err = client.Stop();
    ASSERT_TRUE(err.IsNone()) << err.Message();

    // 9) Wait for disconnect
    err = mSMInfoReceiver.WaitDisconnect(cMainNodeID);
    EXPECT_TRUE(err.IsNone()) << err.Message();
}

TEST_F(SMControllerTest, GetBlobsInfos)
{
    const std::string digest = "sha256:1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef";
    const std::string url    = "https://example.com/blob.tar";

    // 1) Setup blob URL in stub
    mItemInfoProvider.SetBlobURL(digest, url);

    // 2) Start client
    SMClientStub client;

    auto err = client.Init(cMainNodeID);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    err = client.Start(mConfig.mCMServerURL);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    // 3) Wait for SM info
    err = mSMInfoReceiver.WaitSMInfo(cMainNodeID);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    // 4) Call GetBlobsInfos
    servicemanager::v5::BlobsInfos responseBlobsInfos;
    std::vector<std::string>       digests = {digest};
    ASSERT_TRUE(client.GetBlobsInfos(digests, responseBlobsInfos).IsNone());

    ASSERT_EQ(responseBlobsInfos.urls_size(), 1);
    EXPECT_EQ(responseBlobsInfos.urls(0), url);

    // 5) Stop client
    err = client.Stop();
    ASSERT_TRUE(err.IsNone()) << err.Message();

    // 6) Wait for disconnect
    err = mSMInfoReceiver.WaitDisconnect(cMainNodeID);
    EXPECT_TRUE(err.IsNone()) << err.Message();
}

TEST_F(SMControllerTest, SubscribeInstanceNetworkUpdates_ReceivesPendingFirewallUpdate)
{
    SMClientStub client;

    auto err = client.Init(cMainNodeID);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    err = client.Start(mConfig.mCMServerURL);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    err = mSMInfoReceiver.WaitSMInfo(cMainNodeID);
    ASSERT_TRUE(err.IsNone()) << err.Message();

    err = client.SubscribeInstanceNetworkUpdates();
    ASSERT_TRUE(err.IsNone()) << err.Message();

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    aos::networkmanager::PendingFirewallUpdate update;
    update.mInstanceIdent.mItemID    = "serviceA";
    update.mInstanceIdent.mSubjectID = "subject1";
    update.mInstanceIdent.mInstance  = 1;

    FirewallRule rule;
    rule.mDstIP   = "10.0.0.5";
    rule.mDstPort = "8080";
    rule.mProto   = "tcp";
    rule.mSrcIP   = "192.168.1.2";
    update.mFirewallRules.PushBack(rule);

    mSMController.OnPendingFirewallUpdate(cMainNodeID, update);

    err = client.WaitPendingFirewallUpdate();
    ASSERT_TRUE(err.IsNone()) << err.Message();

    auto& received = client.GetReceivedUpdates();
    ASSERT_EQ(received.size(), 1);
    EXPECT_EQ(received[0].instance().item_id(), "serviceA");
    EXPECT_EQ(received[0].instance().subject_id(), "subject1");
    EXPECT_EQ(received[0].instance().instance(), 1);
    ASSERT_EQ(received[0].firewall_rules_size(), 1);
    EXPECT_EQ(received[0].firewall_rules(0).dst_ip(), "10.0.0.5");
    EXPECT_EQ(received[0].firewall_rules(0).dst_port(), "8080");
    EXPECT_EQ(received[0].firewall_rules(0).proto(), "tcp");
    EXPECT_EQ(received[0].firewall_rules(0).src_ip(), "192.168.1.2");

    err = client.Stop();
    ASSERT_TRUE(err.IsNone()) << err.Message();

    err = mSMInfoReceiver.WaitDisconnect(cMainNodeID);
    EXPECT_TRUE(err.IsNone()) << err.Message();
}

} // namespace aos::cm::smcontroller
