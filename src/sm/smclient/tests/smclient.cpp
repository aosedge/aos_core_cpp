/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <future>

#include <gmock/gmock.h>
#include <google/protobuf/text_format.h>
#include <google/protobuf/util/message_differencer.h>
#include <grpcpp/server_builder.h>

#include <core/common/tests/mocks/connectionprovidermock.hpp>
#include <core/common/tests/mocks/cryptomock.hpp>
#include <core/common/tests/mocks/logprovidermock.hpp>
#include <core/common/tests/mocks/monitoringmock.hpp>
#include <core/common/tests/utils/log.hpp>
#include <core/iam/tests/mocks/certhandlermock.hpp>
#include <core/iam/tests/mocks/nodeinfoprovidermock.hpp>
#include <core/iam/tests/mocks/provisionmanagermock.hpp>
#include <core/sm/tests/mocks/launchermock.hpp>
#include <core/sm/tests/mocks/networkmanagermock.hpp>
#include <core/sm/tests/mocks/resourcemanagermock.hpp>

#include <servicemanager/v4/servicemanager.grpc.pb.h>

#include <common/tests/mocks/iamclientmock.hpp>
#include <sm/smclient/smclient.hpp>

using namespace testing;
using namespace aos;

/***********************************************************************************************************************
 * Test utils
 **********************************************************************************************************************/

namespace common::v1 {

bool operator==(const ErrorInfo& lhs, const ErrorInfo& rhs)
{
    return google::protobuf::util::MessageDifferencer::Equals(lhs, rhs);
}

} // namespace common::v1

namespace servicemanager::v4 {
bool operator==(const smproto::NodeConfigStatus& lhs, const smproto::NodeConfigStatus& rhs)
{
    return google::protobuf::util::MessageDifferencer::Equals(lhs, rhs);
}

} // namespace servicemanager::v4

namespace {

aos::NodeInfoObsolete CreateNodeInfo()
{
    aos::NodeInfoObsolete result;

    result.mNodeID = "test-node-id";

    return result;
}

aos::monitoring::NodeMonitoringData CreateNodeMonitoringData()
{
    aos::monitoring::NodeMonitoringData monitoringData;

    monitoringData.mNodeID                   = "test-node-id";
    monitoringData.mMonitoringData.mCPU      = 1;
    monitoringData.mMonitoringData.mRAM      = 2;
    monitoringData.mMonitoringData.mDownload = 3;
    monitoringData.mMonitoringData.mUpload   = 4;

    return monitoringData;
}

aos::AlertVariant CreateAlert()
{
    aos::AlertVariant result;
    aos::SystemAlert  systemAlert;

    systemAlert.mMessage = "test-message";

    result.SetValue<aos::SystemAlert>(systemAlert);

    return result;
}

aos::PushLog CreatePushLog()
{
    aos::PushLog log;

    log.mContent    = "test log";
    log.mLogID      = "test-log-id";
    log.mPart       = 0;
    log.mPartsCount = 1;

    return log;
}

aos::InstanceStatusStaticArray CreateInstanceStatus()
{
    aos::InstanceStatusStaticArray instances;
    aos::InstanceStatus            instance;

    instance.mInstanceIdent = aos::InstanceIdent {"service-id", "instance-id", 0};
    instance.mState         = aos::InstanceStateEnum::eActive;
    instance.mVersion       = "1.0.0";

    instances.PushBack(instance);

    return instances;
}

} // namespace

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class TestSMService : public smproto::SMService::Service {
public:
    TestSMService(const std::string& url) { mServer = CreateServer(url, grpc::InsecureServerCredentials()); }

    ~TestSMService()
    {
        if (mCtx) {
            mCtx->TryCancel();
        }
    }

    grpc::Status RegisterSM(grpc::ServerContext*                                            context,
        grpc::ServerReaderWriter<smproto::SMIncomingMessages, smproto::SMOutgoingMessages>* stream) override
    {
        LOG_INF() << "Test server message thread started";

        try {

            mStream = stream;
            mCtx    = context;

            smproto::SMOutgoingMessages incomingMsg;

            while (stream->Read(&incomingMsg)) {
                {
                    if (incomingMsg.has_node_config_status()) {
                        std::lock_guard lock {mLock};

                        OnNodeConfigStatus(incomingMsg.node_config_status());

                        mNodeConfigStatusReceived = true;
                        mNodeConfigStatusCV.notify_all();
                        continue;
                    } else if (incomingMsg.has_run_instances_status()) {
                        OnRunInstancesStatus(incomingMsg.run_instances_status());
                    } else if (incomingMsg.has_update_instances_status()) {
                        OnUpdateInstancesStatus(incomingMsg.update_instances_status());
                    } else if (incomingMsg.has_override_env_var_status()) {
                        OnOverrideEnvVarStatus(incomingMsg.override_env_var_status());
                    } else if (incomingMsg.has_log()) {
                        OnLogData(incomingMsg.log());
                    } else if (incomingMsg.has_instant_monitoring()) {
                        OnInstantMonitoring(incomingMsg.instant_monitoring());
                    } else if (incomingMsg.has_average_monitoring()) {
                        OnAverageMonitoring(incomingMsg.average_monitoring());
                    } else if (incomingMsg.has_alert()) {
                        OnAlert(incomingMsg.alert());
                    } else if (incomingMsg.has_image_content_request()) {
                        OnImageContentRequest(incomingMsg.image_content_request());
                    } else if (incomingMsg.has_clock_sync_request()) {
                        OnClockSyncRequest(incomingMsg.clock_sync_request());
                    } else {
                        LOG_ERR() << "Unknown message received in test server";

                        continue;
                    }

                    {
                        std::lock_guard lock {mLock};

                        mResponseCV.notify_all();
                        mResponseReceived = true;
                    }
                }
            }
        } catch (const std::exception& e) {
            LOG_ERR() << e.what();
        }

        LOG_DBG() << "Test server message thread stoped";

        mStream = nullptr;
        mCtx    = nullptr;

        return grpc::Status::OK;
    }

    MOCK_METHOD(void, OnNodeConfigStatus, (const smproto::NodeConfigStatus&));
    MOCK_METHOD(void, OnRunInstancesStatus, (const smproto::RunInstancesStatus&));
    MOCK_METHOD(void, OnUpdateInstancesStatus, (const smproto::UpdateInstancesStatus&));
    MOCK_METHOD(void, OnOverrideEnvVarStatus, (const smproto::OverrideEnvVarStatus&));
    MOCK_METHOD(void, OnLogData, (const smproto::LogData&));
    MOCK_METHOD(void, OnInstantMonitoring, (const smproto::InstantMonitoring&));
    MOCK_METHOD(void, OnAverageMonitoring, (const smproto::AverageMonitoring&));
    MOCK_METHOD(void, OnAlert, (const smproto::Alert&));
    MOCK_METHOD(void, OnImageContentRequest, (const smproto::ImageContentRequest&));
    MOCK_METHOD(void, OnClockSyncRequest, (const smproto::ClockSyncRequest&));

    void GetNodeConfigStatus()
    {
        smproto::SMIncomingMessages incomingMsg;

        incomingMsg.mutable_get_node_config_status();

        mStream->Write(incomingMsg);
    }

    void CheckNodeConfig(const std::string& version, const std::string& config)
    {
        smproto::SMIncomingMessages incomingMsg;

        auto checkNodeConfig = incomingMsg.mutable_check_node_config();

        checkNodeConfig->set_version(version);
        checkNodeConfig->set_node_config(config);

        mStream->Write(incomingMsg);
    }

    void SetNodeConfig(const std::string& version, const std::string& config)
    {
        smproto::SMIncomingMessages incomingMsg;

        auto setNodeConfig = incomingMsg.mutable_set_node_config();

        setNodeConfig->set_version(version);
        setNodeConfig->set_node_config(config);

        mStream->Write(incomingMsg);
    }

    void RunInstances(const servicemanager::v4::RunInstances& runInstances = servicemanager::v4::RunInstances {})
    {
        smproto::SMIncomingMessages incomingMsg;

        incomingMsg.mutable_run_instances()->CopyFrom(runInstances);

        mStream->Write(incomingMsg);
    }

    void UpdateNetwork(const servicemanager::v4::UpdateNetworks& updateNetwork = servicemanager::v4::UpdateNetworks {})
    {
        smproto::SMIncomingMessages incomingMsg;

        incomingMsg.mutable_update_networks()->CopyFrom(updateNetwork);

        mStream->Write(incomingMsg);
    }

    void GetSystemLog()
    {
        smproto::SMIncomingMessages incomingMsg;

        incomingMsg.mutable_system_log_request();

        mStream->Write(incomingMsg);
    }

    void GetInstanceLog()
    {
        smproto::SMIncomingMessages incomingMsg;

        incomingMsg.mutable_instance_log_request();

        mStream->Write(incomingMsg);
    }

    void GetInstanceCrashLog()
    {
        smproto::SMIncomingMessages incomingMsg;

        incomingMsg.mutable_instance_crash_log_request();

        mStream->Write(incomingMsg);
    }

    void OverrideEnvVars(
        const servicemanager::v4::OverrideEnvVars& overrideEnvVars = servicemanager::v4::OverrideEnvVars {})
    {
        smproto::SMIncomingMessages incomingMsg;

        incomingMsg.mutable_override_env_vars()->CopyFrom(overrideEnvVars);

        mStream->Write(incomingMsg);
    }

    void GetAverageMonitoring()
    {
        smproto::SMIncomingMessages incomingMsg;

        incomingMsg.mutable_get_average_monitoring();

        mStream->Write(incomingMsg);
    }

    void SendConnectionStatus(smproto::ConnectionEnum status = smproto::ConnectionEnum::CONNECTED)
    {
        smproto::SMIncomingMessages incomingMsg;

        incomingMsg.mutable_connection_status()->set_cloud_status(status);

        mStream->Write(incomingMsg);
    }

    void WaitNodeConfigStatus(const std::chrono::seconds& timeout = std::chrono::seconds(4))
    {
        std::unique_lock lock {mLock};

        mNodeConfigStatusCV.wait_for(lock, timeout, [this] { return mNodeConfigStatusReceived; });

        mNodeConfigStatusReceived = false;
    }

    void WaitMessage(const std::chrono::seconds& timeout = std::chrono::seconds(4))
    {
        std::unique_lock lock {mLock};

        mResponseCV.wait_for(lock, timeout, [this] { return mResponseReceived; });

        mResponseReceived = false;
    }

    aos::Error SendMessage(const smproto::SMIncomingMessages& msg)
    {
        if (!mStream) {
            return AOS_ERROR_WRAP(aos::Error(ErrorEnum::eFailed, "stream is not initialized"));
        }

        if (!mStream->Write(msg)) {
            return AOS_ERROR_WRAP(aos::Error(ErrorEnum::eFailed, "can't send message"));
        }

        return aos::ErrorEnum::eNone;
    }

private:
    std::unique_ptr<grpc::Server> CreateServer(
        const std::string& addr, const std::shared_ptr<grpc::ServerCredentials>& credentials)
    {
        grpc::ServerBuilder builder;

        builder.AddListeningPort(addr, credentials);
        builder.RegisterService(static_cast<smproto::SMService::Service*>(this));

        return builder.BuildAndStart();
    }

    grpc::ServerReaderWriter<smproto::SMIncomingMessages, smproto::SMOutgoingMessages>* mStream = nullptr;
    grpc::ServerContext*                                                                mCtx    = nullptr;

    std::mutex              mLock;
    std::condition_variable mNodeConfigStatusCV;
    std::condition_variable mResponseCV;

    bool mNodeConfigStatusReceived = false;
    bool mResponseReceived         = false;

    std::unique_ptr<grpc::Server> mServer;
};

class SMClientTest : public Test {
protected:
    void SetUp() override { tests::utils::InitLog(); }

    static sm::smclient::Config GetConfig()
    {
        sm::smclient::Config config;

        config.mCMServerURL        = "localhost:5555";
        config.mCertStorage        = "sm";
        config.mCMReconnectTimeout = 100 * Time::cMilliseconds;

        return config;
    }

    std::unique_ptr<sm::smclient::SMClient> CreateClient(
        const sm::smclient::Config& config = GetConfig(), bool secureConnection = false)
    {
        auto client = std::make_unique<sm::smclient::SMClient>();

        auto err = client->Init(config, mTLSCredentials, mNodeInfoProvider, mResourceManager, mNetworkManager,
            mLogProvider, mResourceMonitor, mLauncher, secureConnection);

        if (!err.IsNone()) {
            LOG_ERR() << "Can't init client: error=" << err.Message();

            return nullptr;
        }

        return client;
    }

    std::unique_ptr<TestSMService> CreateServer(const std::string& url) { return std::make_unique<TestSMService>(url); }

    std::pair<std::unique_ptr<TestSMService>, std::unique_ptr<sm::smclient::SMClient>> InitTest(
        const sm::smclient::Config& config = GetConfig(), bool provisionMode = true)
    {
        auto server = CreateServer(config.mCMServerURL);
        auto client = CreateClient(config);

        NodeInfoObsolete                        nodeInfo          = CreateNodeInfo();
        RetWithError<StaticString<cVersionLen>> nodeConfigVersion = {"1.0.0", ErrorEnum::eNone};
        smproto::NodeConfigStatus               expNodeConfigVersion;

        expNodeConfigVersion.set_node_id(nodeInfo.mNodeID.CStr());
        expNodeConfigVersion.set_node_type(nodeInfo.mNodeType.CStr());
        expNodeConfigVersion.set_version(nodeConfigVersion.mValue.CStr());

        EXPECT_CALL(mNodeInfoProvider, GetNodeInfo)
            .WillRepeatedly(DoAll(SetArgReferee<0>(nodeInfo), Return(ErrorEnum::eNone)));
        EXPECT_CALL(mLauncher, GetCurrentRunStatus).WillRepeatedly(Return(ErrorEnum::eNone));
        EXPECT_CALL(mTLSCredentials, GetTLSClientCredentials)
            .WillRepeatedly(Return(std::shared_ptr<grpc::ChannelCredentials>()));
        EXPECT_CALL(mResourceManager, GetNodeConfigVersion).WillRepeatedly(Return(nodeConfigVersion));
        EXPECT_CALL(*server, OnNodeConfigStatus(expNodeConfigVersion)).Times(1);

        if (!provisionMode) {
            EXPECT_CALL(mTLSCredentials, SubscribeCertChanged).WillOnce(Return(ErrorEnum::eNone));
            EXPECT_CALL(mTLSCredentials, UnsubscribeCertChanged).WillOnce(Return(ErrorEnum::eNone));
        }

        EXPECT_CALL(mLogProvider, Subscribe(_)).WillOnce(Return(Error()));

        if (auto err = client->Start(); !err.IsNone()) {
            LOG_ERR() << "Can't start client: error=" << err.Message();

            return std::make_pair(nullptr, nullptr);
        }

        server->WaitNodeConfigStatus();
        server->WaitMessage();

        return std::make_pair(std::move(server), std::move(client));
    }

    aos::common::iamclient::TLSCredentialsMock  mTLSCredentials;
    iam::nodeinfoprovider::NodeInfoProviderMock mNodeInfoProvider;
    sm::resourcemanager::ResourceManagerMock    mResourceManager;
    sm::networkmanager::NetworkManagerMock      mNetworkManager;
    sm::logprovider::LogProviderMock            mLogProvider;
    monitoring::ResourceMonitorMock             mResourceMonitor;
    sm::launcher::LauncherMock                  mLauncher;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(SMClientTest, ClientNotStarted)
{
    auto server = CreateServer(GetConfig().mCMServerURL);
    ASSERT_NE(server, nullptr) << "Can't create server";

    auto client = CreateClient();
    ASSERT_NE(client, nullptr) << "Can't create client";

    EXPECT_CALL(*server, OnNodeConfigStatus).Times(0);
    server->WaitNodeConfigStatus(std::chrono::seconds(1));

    auto err = client->Stop();
    ASSERT_TRUE(err.IsNone()) << "Stop should return no error if start wasn't called" << err.Message();
}

TEST_F(SMClientTest, SecondStartReturnsError)
{
    auto [server, client] = InitTest();

    auto err = client->Start();
    ASSERT_TRUE(err.Is(aos::ErrorEnum::eFailed))
        << "Start should return failed if client isn't closed" << err.Message();

    EXPECT_CALL(mLogProvider, Unsubscribe(_)).WillOnce(Return(Error()));
    err = client->Stop();
    ASSERT_TRUE(err.IsNone()) << "Can't stop client: error=" << err.Message();
}

TEST_F(SMClientTest, ClientReconnectOnGettingUnhandlerMessage)
{
    auto config = GetConfig();

    config.mCMReconnectTimeout = 10 * Time::cMilliseconds;

    auto [server, client] = InitTest(config);

    server->SendMessage(smproto::SMIncomingMessages());

    // Client is expected to reconnect and send node config status
    EXPECT_CALL(*server, OnNodeConfigStatus).Times(1);
    server->WaitNodeConfigStatus(std::chrono::seconds(3));

    EXPECT_CALL(mLogProvider, Unsubscribe(_)).WillOnce(Return(Error()));

    auto err = client->Stop();
    ASSERT_TRUE(err.IsNone()) << "Can't stop client: error=" << err.Message();
}

TEST_F(SMClientTest, StartFailsOnGetNodeInfoError)
{
    auto server = CreateServer(GetConfig().mCMServerURL);
    ASSERT_NE(server, nullptr) << "Can't create server";

    auto client = CreateClient();
    ASSERT_NE(client, nullptr) << "Can't create client";

    EXPECT_CALL(mNodeInfoProvider, GetNodeInfo).WillOnce(Return(aos::ErrorEnum::eFailed));

    auto err = client->Start();
    ASSERT_TRUE(err.Is(aos::ErrorEnum::eFailed))
        << "Start should return failed if get node info fails: error=" << err.Message();
}

TEST_F(SMClientTest, SendMonitoringDataSucceeds)
{
    auto [server, client] = InitTest();

    EXPECT_CALL(*server, OnInstantMonitoring).Times(1);

    auto err = client->SendMonitoringData(CreateNodeMonitoringData());
    EXPECT_TRUE(err.IsNone()) << "Can't send monitoring data: error=" << err.Message();

    server->WaitMessage();

    EXPECT_CALL(mLogProvider, Unsubscribe(_)).WillOnce(Return(Error()));

    err = client->Stop();
    ASSERT_TRUE(err.IsNone()) << "Can't stop client: error=" << err.Message();
}

TEST_F(SMClientTest, SendMonitoringDataReturnsErrorIfNotConnected)
{
    auto client = CreateClient();
    ASSERT_NE(client, nullptr) << "Can't create client";

    auto err = client->SendMonitoringData(CreateNodeMonitoringData());
    EXPECT_TRUE(err.Is(aos::ErrorEnum::eFailed)) << "Client should fail: error=" << err.Message();
}

TEST_F(SMClientTest, SendAlertSucceeds)
{
    auto [server, client] = InitTest();

    EXPECT_CALL(*server, OnAlert).Times(1);

    auto err = client->SendAlert(CreateAlert());
    EXPECT_TRUE(err.IsNone()) << "Can't send alerts: error=" << err.Message();

    server->WaitMessage();

    EXPECT_CALL(mLogProvider, Unsubscribe(_)).WillOnce(Return(Error()));

    err = client->Stop();
    ASSERT_TRUE(err.IsNone()) << "Can't stop client: error=" << err.Message();
}

TEST_F(SMClientTest, SendAlertReturnsErrorIfNotConnected)
{
    auto client = CreateClient();
    ASSERT_NE(client, nullptr) << "Can't create client";

    auto err = client->SendAlert(CreateAlert());
    EXPECT_TRUE(err.Is(aos::ErrorEnum::eFailed)) << "Client should fail: error=" << err.Message();
}

TEST_F(SMClientTest, OnLogReceivedSucceeds)
{
    auto [server, client] = InitTest();

    EXPECT_CALL(*server, OnLogData).Times(1);

    auto err = client->OnLogReceived(CreatePushLog());
    EXPECT_TRUE(err.IsNone()) << "Can't send log data: error=" << err.Message();

    server->WaitMessage();

    EXPECT_CALL(mLogProvider, Unsubscribe(_)).WillOnce(Return(Error()));

    err = client->Stop();
    ASSERT_TRUE(err.IsNone()) << "Can't stop client: error=" << err.Message();
}

TEST_F(SMClientTest, OnLogReceivedReturnsErrorIfNotConnected)
{
    auto client = CreateClient();
    ASSERT_NE(client, nullptr) << "Can't create client";

    auto err = client->OnLogReceived(CreatePushLog());
    EXPECT_TRUE(err.Is(aos::ErrorEnum::eFailed)) << "Client should fail: error=" << err.Message();
}

TEST_F(SMClientTest, InstancesRunStatusSucceeds)
{
    auto [server, client] = InitTest();

    EXPECT_CALL(*server, OnRunInstancesStatus).Times(1);

    auto err = client->InstancesRunStatus(CreateInstanceStatus());
    EXPECT_TRUE(err.IsNone()) << "Can't send instance run status: error=" << err.Message();

    server->WaitMessage();

    EXPECT_CALL(mLogProvider, Unsubscribe(_)).WillOnce(Return(Error()));

    err = client->Stop();
    ASSERT_TRUE(err.IsNone()) << "Can't stop client: error=" << err.Message();
}

TEST_F(SMClientTest, InstancesRunStatusReturnsErrorIfNotConnected)
{
    auto client = CreateClient();
    ASSERT_NE(client, nullptr) << "Can't create client";

    auto err = client->InstancesRunStatus(CreateInstanceStatus());
    EXPECT_TRUE(err.Is(aos::ErrorEnum::eFailed)) << "Client should fail: error=" << err.Message();
}

TEST_F(SMClientTest, InstancesUpdateStatusSucceeds)
{
    auto [server, client] = InitTest();

    auto err = client->InstancesUpdateStatus(CreateInstanceStatus());
    EXPECT_TRUE(err.IsNone()) << "Can't send instance update status: error=" << err.Message();

    server->WaitMessage(std::chrono::seconds(1));

    EXPECT_CALL(mLogProvider, Unsubscribe(_)).WillOnce(Return(Error()));

    err = client->Stop();
    ASSERT_TRUE(err.IsNone()) << "Can't stop client: error=" << err.Message();
}

TEST_F(SMClientTest, InstancesUpdateStatusReturnsErrorIfNotConnected)
{
    auto client = CreateClient();
    ASSERT_NE(client, nullptr) << "Can't create client";

    auto err = client->InstancesUpdateStatus(CreateInstanceStatus());
    EXPECT_TRUE(err.Is(aos::ErrorEnum::eFailed)) << "Client should fail: error=" << err.Message();
}

TEST_F(SMClientTest, GetNodeConfigStatusSucceeds)
{
    auto [server, client] = InitTest();

    EXPECT_CALL(*server, OnNodeConfigStatus).Times(1);

    server->GetNodeConfigStatus();

    server->WaitNodeConfigStatus();

    EXPECT_CALL(mLogProvider, Unsubscribe(_)).WillOnce(Return(Error()));

    auto err = client->Stop();
    ASSERT_TRUE(err.IsNone()) << "Can't stop client: error=" << err.Message();
}

TEST_F(SMClientTest, CheckNodeConfigSucceeds)
{
    auto [server, client] = InitTest();

    EXPECT_CALL(*server, OnNodeConfigStatus).Times(1);
    EXPECT_CALL(mResourceManager, CheckNodeConfig).WillOnce(Return(ErrorEnum::eNone));

    server->CheckNodeConfig("1.0.1", "{}");

    server->WaitNodeConfigStatus();

    EXPECT_CALL(mLogProvider, Unsubscribe(_)).WillOnce(Return(Error()));

    auto err = client->Stop();
    ASSERT_TRUE(err.IsNone()) << "Can't stop client: error=" << err.Message();
}

TEST_F(SMClientTest, SetNodeConfigSucceeds)
{
    auto [server, client] = InitTest();

    EXPECT_CALL(*server, OnNodeConfigStatus).Times(1);
    EXPECT_CALL(mResourceManager, UpdateNodeConfig).WillOnce(Return(ErrorEnum::eNone));

    server->SetNodeConfig("1.0.1", "{}");

    server->WaitNodeConfigStatus();

    EXPECT_CALL(mLogProvider, Unsubscribe(_)).WillOnce(Return(Error()));

    auto err = client->Stop();
    ASSERT_TRUE(err.IsNone()) << "Can't stop client: error=" << err.Message();
}

TEST_F(SMClientTest, RunInstancesSucceeds)
{
    auto [server, client] = InitTest();

    EXPECT_CALL(*server, OnRunInstancesStatus).Times(1);
    EXPECT_CALL(mLauncher, RunInstances).WillOnce(Invoke([&] {
        client->InstancesRunStatus(CreateInstanceStatus());

        return ErrorEnum::eNone;
    }));

    server->RunInstances();

    server->WaitMessage();

    EXPECT_CALL(mLogProvider, Unsubscribe(_)).WillOnce(Return(Error()));

    auto err = client->Stop();
    ASSERT_TRUE(err.IsNone()) << "Can't stop client: error=" << err.Message();
}

TEST_F(SMClientTest, ClientReconnectsOnRunInstancesServicesExceedsLimit)
{
    auto [server, client] = InitTest();

    EXPECT_CALL(*server, OnNodeConfigStatus).Times(1);
    EXPECT_CALL(*server, OnRunInstancesStatus).Times(1);
    EXPECT_CALL(mLauncher, RunInstances).Times(0);

    servicemanager::v4::RunInstances runInstances;
    for (size_t i = 0; i < aos::cMaxNumServices + 1; ++i) {
        runInstances.add_services();
    }

    server->RunInstances(runInstances);

    server->WaitNodeConfigStatus();

    EXPECT_CALL(mLogProvider, Unsubscribe(_)).WillOnce(Return(Error()));

    auto err = client->Stop();
    ASSERT_TRUE(err.IsNone()) << "Can't stop client: error=" << err.Message();
}

TEST_F(SMClientTest, ClientReconnectsOnRunInstancesLayersExceedsLimit)
{
    auto [server, client] = InitTest();

    EXPECT_CALL(*server, OnNodeConfigStatus).Times(1);
    EXPECT_CALL(*server, OnRunInstancesStatus).Times(1);
    EXPECT_CALL(mLauncher, RunInstances).Times(0);

    servicemanager::v4::RunInstances runInstances;
    for (size_t i = 0; i < aos::cMaxNumLayers + 1; ++i) {
        runInstances.add_layers();
    }

    server->RunInstances(runInstances);

    server->WaitNodeConfigStatus();

    auto err = client->Stop();
    ASSERT_TRUE(err.IsNone()) << "Can't stop client: error=" << err.Message();
}

TEST_F(SMClientTest, ClientReconnectsOnRunInstancesInstancesExceedsLimit)
{
    auto [server, client] = InitTest();

    EXPECT_CALL(*server, OnNodeConfigStatus).Times(1);
    EXPECT_CALL(*server, OnRunInstancesStatus).Times(1);
    EXPECT_CALL(mLauncher, RunInstances).Times(0);

    servicemanager::v4::RunInstances runInstances;
    for (size_t i = 0; i < aos::cMaxNumInstances + 1; ++i) {
        runInstances.add_instances();
    }

    server->RunInstances(runInstances);

    server->WaitNodeConfigStatus();

    EXPECT_CALL(mLogProvider, Unsubscribe(_)).WillOnce(Return(Error()));

    auto err = client->Stop();
    ASSERT_TRUE(err.IsNone()) << "Can't stop client: error=" << err.Message();
}

TEST_F(SMClientTest, ClientReconnectsOnRunInstancesLauncherError)
{
    auto [server, client] = InitTest();

    EXPECT_CALL(*server, OnNodeConfigStatus).Times(1);
    EXPECT_CALL(*server, OnRunInstancesStatus).Times(1);
    EXPECT_CALL(mLauncher, RunInstances).WillOnce(Return(aos::ErrorEnum::eFailed));

    server->RunInstances();

    server->WaitNodeConfigStatus();

    EXPECT_CALL(mLogProvider, Unsubscribe(_)).WillOnce(Return(Error()));

    auto err = client->Stop();
    ASSERT_TRUE(err.IsNone()) << "Can't stop client: error=" << err.Message();
}

TEST_F(SMClientTest, UpdateNetworkSucceeds)
{
    auto [server, client] = InitTest();

    std::promise<void> promise;

    EXPECT_CALL(mNetworkManager, UpdateNetworks).WillOnce(Invoke([&] {
        promise.set_value();

        return ErrorEnum::eNone;
    }));

    server->UpdateNetwork();

    auto status = promise.get_future().wait_for(std::chrono::seconds(1));
    EXPECT_EQ(status, std::future_status::ready) << "network manager wasn't called";

    EXPECT_CALL(mLogProvider, Unsubscribe(_)).WillOnce(Return(Error()));

    auto err = client->Stop();
    ASSERT_TRUE(err.IsNone()) << "Can't stop client: error=" << err.Message();
}

TEST_F(SMClientTest, ClientReconnectsOnUpdateNetworkManagerError)
{
    auto [server, client] = InitTest();

    EXPECT_CALL(*server, OnNodeConfigStatus).Times(1);
    EXPECT_CALL(mNetworkManager, UpdateNetworks).WillOnce(Return(aos::ErrorEnum::eFailed));

    server->UpdateNetwork();

    server->WaitNodeConfigStatus();

    EXPECT_CALL(mLogProvider, Unsubscribe(_)).WillOnce(Return(Error()));

    auto err = client->Stop();
    ASSERT_TRUE(err.IsNone()) << "Can't stop client: error=" << err.Message();
}

TEST_F(SMClientTest, GetSystemLogSucceeds)
{
    auto [server, client] = InitTest();

    EXPECT_CALL(*server, OnLogData).Times(1);
    EXPECT_CALL(mLogProvider, GetSystemLog).WillOnce(Invoke([&] {
        client->OnLogReceived(CreatePushLog());

        return ErrorEnum::eNone;
    }));

    server->GetSystemLog();

    server->WaitMessage();

    EXPECT_CALL(mLogProvider, Unsubscribe(_)).WillOnce(Return(Error()));

    auto err = client->Stop();
    ASSERT_TRUE(err.IsNone()) << "Can't stop client: error=" << err.Message();
}

TEST_F(SMClientTest, ClientReconnectsOnGetSystemLogProviderError)
{
    auto [server, client] = InitTest();

    EXPECT_CALL(*server, OnNodeConfigStatus).Times(1);
    EXPECT_CALL(*server, OnLogData).Times(0);
    EXPECT_CALL(mLogProvider, GetSystemLog).WillOnce(Return(aos::ErrorEnum::eFailed));

    server->GetSystemLog();

    server->WaitNodeConfigStatus();

    EXPECT_CALL(mLogProvider, Unsubscribe(_)).WillOnce(Return(Error()));

    auto err = client->Stop();
    ASSERT_TRUE(err.IsNone()) << "Can't stop client: error=" << err.Message();
}

TEST_F(SMClientTest, GetInstanceLogSucceeds)
{
    auto [server, client] = InitTest();

    EXPECT_CALL(*server, OnLogData).Times(1);
    EXPECT_CALL(mLogProvider, GetInstanceLog).WillOnce(Invoke([&] {
        client->OnLogReceived(CreatePushLog());

        return ErrorEnum::eNone;
    }));

    server->GetInstanceLog();

    server->WaitMessage();

    EXPECT_CALL(mLogProvider, Unsubscribe(_)).WillOnce(Return(Error()));

    auto err = client->Stop();
    ASSERT_TRUE(err.IsNone()) << "Can't stop client: error=" << err.Message();
}

TEST_F(SMClientTest, ClientReconnectsOnGetInstanceLogProviderError)
{
    auto [server, client] = InitTest();

    EXPECT_CALL(*server, OnNodeConfigStatus).Times(1);
    EXPECT_CALL(*server, OnLogData).Times(0);
    EXPECT_CALL(mLogProvider, GetInstanceLog).WillOnce(Return(aos::ErrorEnum::eFailed));

    server->GetInstanceLog();

    server->WaitNodeConfigStatus();

    EXPECT_CALL(mLogProvider, Unsubscribe(_)).WillOnce(Return(Error()));

    auto err = client->Stop();
    ASSERT_TRUE(err.IsNone()) << "Can't stop client: error=" << err.Message();
}

TEST_F(SMClientTest, GetInstanceCrashLogSucceeds)
{
    auto [server, client] = InitTest();

    EXPECT_CALL(*server, OnLogData).Times(1);
    EXPECT_CALL(mLogProvider, GetInstanceCrashLog).WillOnce(Invoke([&] {
        client->OnLogReceived(CreatePushLog());

        return ErrorEnum::eNone;
    }));

    server->GetInstanceCrashLog();

    server->WaitMessage();

    EXPECT_CALL(mLogProvider, Unsubscribe(_)).WillOnce(Return(Error()));

    auto err = client->Stop();
    ASSERT_TRUE(err.IsNone()) << "Can't stop client: error=" << err.Message();
}

TEST_F(SMClientTest, ClientReconnectsOnGetInstanceCrashLogProviderError)
{
    auto [server, client] = InitTest();

    EXPECT_CALL(*server, OnNodeConfigStatus).Times(1);
    EXPECT_CALL(*server, OnLogData).Times(0);
    EXPECT_CALL(mLogProvider, GetInstanceCrashLog).WillOnce(Return(aos::ErrorEnum::eFailed));

    server->GetInstanceCrashLog();

    server->WaitNodeConfigStatus();

    EXPECT_CALL(mLogProvider, Unsubscribe(_)).WillOnce(Return(Error()));

    auto err = client->Stop();
    ASSERT_TRUE(err.IsNone()) << "Can't stop client: error=" << err.Message();
}

TEST_F(SMClientTest, OverrideEnvVarsSucceeds)
{
    auto [server, client] = InitTest();

    aos::EnvVarsInstanceStatus expectedStatus;

    expectedStatus.mItemID = "service-id";
    expectedStatus.mStatuses.PushBack({"var-name", aos::ErrorEnum::eNone});

    EXPECT_CALL(*server, OnOverrideEnvVarStatus).WillOnce(Invoke([&](const smproto::OverrideEnvVarStatus& status) {
        EXPECT_EQ(status.error().aos_code(), static_cast<int32_t>(aos::ErrorEnum::eNone));

        EXPECT_EQ(status.env_vars_status().size(), 1);
        EXPECT_EQ(status.env_vars_status(0).instance_filter().service_id(), "service-id");
        EXPECT_EQ(status.env_vars_status(0).instance_filter().instance(), 0);

        EXPECT_EQ(status.env_vars_status(0).statuses().size(), 1);
        EXPECT_EQ(status.env_vars_status(0).statuses(0).name(), "var-name");
        EXPECT_EQ(
            status.env_vars_status(0).statuses(0).error().aos_code(), static_cast<int32_t>(aos::ErrorEnum::eNone));
    }));

    EXPECT_CALL(mLauncher, OverrideEnvVars)
        .WillOnce(Invoke([&](const auto& envVarsInstanceInfos, auto& envVarStatuses) {
            (void)envVarsInstanceInfos;

            envVarStatuses.PushBack(expectedStatus);

            return aos::ErrorEnum::eNone;
        }));

    server->OverrideEnvVars();

    server->WaitMessage();

    EXPECT_CALL(mLogProvider, Unsubscribe(_)).WillOnce(Return(Error()));

    auto err = client->Stop();
    ASSERT_TRUE(err.IsNone()) << "Can't stop client: error=" << err.Message();
}

TEST_F(SMClientTest, OverrideEnvVarsRequestExceedsApplicationLimit)
{
    auto [server, client] = InitTest();

    EXPECT_CALL(*server, OnOverrideEnvVarStatus).WillOnce(Invoke([&](const smproto::OverrideEnvVarStatus& status) {
        EXPECT_EQ(status.error().aos_code(), static_cast<int32_t>(aos::ErrorEnum::eNoMemory));
    }));

    EXPECT_CALL(mLauncher, OverrideEnvVars).Times(0);

    servicemanager::v4::OverrideEnvVars overrideEnvVarsRequest;

    for (size_t i = 0; i < aos::cMaxNumInstances + 1; ++i) {
        overrideEnvVarsRequest.add_env_vars();
    }

    server->OverrideEnvVars(overrideEnvVarsRequest);

    server->WaitMessage();

    EXPECT_CALL(mLogProvider, Unsubscribe(_)).WillOnce(Return(Error()));

    auto err = client->Stop();
    ASSERT_TRUE(err.IsNone()) << "Can't stop client: error=" << err.Message();
}

TEST_F(SMClientTest, OverrideEnvVarsLauncherFails)
{
    auto [server, client] = InitTest();

    EXPECT_CALL(*server, OnOverrideEnvVarStatus).WillOnce(Invoke([&](const smproto::OverrideEnvVarStatus& status) {
        EXPECT_EQ(status.error().aos_code(), static_cast<int32_t>(aos::ErrorEnum::eFailed));
    }));
    EXPECT_CALL(mLauncher, OverrideEnvVars).WillOnce(Return(aos::ErrorEnum::eFailed));

    server->OverrideEnvVars();

    server->WaitMessage();

    EXPECT_CALL(mLogProvider, Unsubscribe(_)).WillOnce(Return(Error()));

    auto err = client->Stop();
    ASSERT_TRUE(err.IsNone()) << "Can't stop client: error=" << err.Message();
}

TEST_F(SMClientTest, GetAverageMonitoringSucceeds)
{
    auto [server, client] = InitTest();

    EXPECT_CALL(*server, OnAverageMonitoring).Times(1);
    EXPECT_CALL(mResourceMonitor, GetAverageMonitoringData)
        .WillOnce(DoAll(SetArgReferee<0>(CreateNodeMonitoringData()), Return(aos::ErrorEnum::eNone)));
    server->GetAverageMonitoring();

    server->WaitMessage();

    EXPECT_CALL(mLogProvider, Unsubscribe(_)).WillOnce(Return(Error()));

    auto err = client->Stop();
    ASSERT_TRUE(err.IsNone()) << "Can't stop client: error=" << err.Message();
}

TEST_F(SMClientTest, ClientReconnectsOnGetAverageMonitoringError)
{
    auto [server, client] = InitTest();

    EXPECT_CALL(*server, OnNodeConfigStatus).Times(1);
    EXPECT_CALL(*server, OnAverageMonitoring).Times(0);
    EXPECT_CALL(mResourceMonitor, GetAverageMonitoringData).WillOnce(Return(aos::ErrorEnum::eFailed));
    server->GetAverageMonitoring();

    server->WaitNodeConfigStatus();

    EXPECT_CALL(mLogProvider, Unsubscribe(_)).WillOnce(Return(Error()));

    auto err = client->Stop();
    ASSERT_TRUE(err.IsNone()) << "Can't stop client: error=" << err.Message();
}

TEST_F(SMClientTest, ConnectionStatusConnectedSucceeds)
{
    auto [server, client] = InitTest();

    ConnectionSubscriberMock subscriber;

    ASSERT_TRUE(client->Subscribe(subscriber).IsNone());

    std::promise<void> promise;

    EXPECT_CALL(subscriber, OnConnect).WillOnce(Invoke([&] { promise.set_value(); }));

    server->SendConnectionStatus(smproto::ConnectionEnum::CONNECTED);

    EXPECT_EQ(promise.get_future().wait_for(std::chrono::seconds(1)), std::future_status::ready)
        << "didn't receive connection status connected";

    client->Unsubscribe(subscriber);

    EXPECT_CALL(mLogProvider, Unsubscribe(_)).WillOnce(Return(Error()));

    auto err = client->Stop();
    ASSERT_TRUE(err.IsNone()) << "Can't stop client: error=" << err.Message();
}

TEST_F(SMClientTest, ConnectionStatusDisconnectedSucceeds)
{
    auto [server, client] = InitTest();

    ConnectionSubscriberMock subscriber;

    ASSERT_TRUE(client->Subscribe(subscriber).IsNone());

    std::promise<void> promise;

    EXPECT_CALL(subscriber, OnDisconnect).WillOnce(Invoke([&] { promise.set_value(); }));

    server->SendConnectionStatus(smproto::ConnectionEnum::DISCONNECTED);

    EXPECT_EQ(promise.get_future().wait_for(std::chrono::seconds(1)), std::future_status::ready)
        << "didn't receive connection status connected";

    client->Unsubscribe(subscriber);

    EXPECT_CALL(mLogProvider, Unsubscribe(_)).WillOnce(Return(Error()));

    auto err = client->Stop();
    ASSERT_TRUE(err.IsNone()) << "Can't stop client: error=" << err.Message();
}
