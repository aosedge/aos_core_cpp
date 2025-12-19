/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <future>
#include <thread>

#include <gmock/gmock.h>
#include <google/protobuf/text_format.h>
#include <google/protobuf/util/message_differencer.h>

#include <core/common/tests/mocks/certprovidermock.hpp>
#include <core/common/tests/utils/log.hpp>

#include <common/iamclient/tests/mocks/tlscredentialsmock.hpp>
#include <core/common/tests/mocks/instancestatusprovidermock.hpp>
#include <core/sm/tests/mocks/launchermock.hpp>
#include <core/sm/tests/mocks/networkmanagermock.hpp>
#include <core/sm/tests/mocks/resourcemanagermock.hpp>
#include <sm/smclient/smclient.hpp>
#include <sm/smclient/tests/mocks/jsonprovidermock.hpp>
#include <sm/smclient/tests/mocks/logprovidermock.hpp>
#include <sm/smclient/tests/mocks/monitoringmock.hpp>
#include <sm/smclient/tests/mocks/nodeconfighandlermock.hpp>
#include <sm/smclient/tests/mocks/runtimeinfoprovidermock.hpp>
#include <sm/smclient/tests/stubs/smservicestub.hpp>

using namespace testing;
using namespace aos;

/***********************************************************************************************************************
 * Test fixture
 **********************************************************************************************************************/

class SMClientTest : public Test {
protected:
    void SetUp() override { tests::utils::InitLog(); }

    static sm::smclient::Config GetConfig()
    {
        sm::smclient::Config config;

        config.mCMServerURL        = "localhost:5556";
        config.mCertStorage        = "sm";
        config.mCMReconnectTimeout = 100 * Time::cMilliseconds;

        return config;
    }

    auto CreateRuntimeInfos()
    {
        auto        runtimes = std::make_unique<RuntimeInfoArray>();
        RuntimeInfo runtime;

        runtime.mRuntimeID   = "runtime1";
        runtime.mRuntimeType = "runc";
        runtime.mMaxDMIPS.SetValue(1000);
        runtime.mAllowedDMIPS.SetValue(800);
        runtime.mTotalRAM.SetValue(1024 * 1024 * 1024);
        runtime.mAllowedRAM.SetValue(512 * 1024 * 1024);
        runtime.mMaxInstances = 10;

        runtimes->PushBack(runtime);

        return runtimes;
    }

    auto CreateResourceInfos()
    {
        auto resources = std::make_unique<StaticArray<sm::resourcemanager::ResourceInfo, 4>>();
        sm::resourcemanager::ResourceInfo resource;

        resource.mName        = "resource1";
        resource.mSharedCount = 2;

        resources->PushBack(resource);

        return resources;
    }

    auto CreateInstanceStatuses()
    {
        auto           statuses = std::make_unique<InstanceStatusArray>();
        InstanceStatus status;

        static_cast<InstanceIdent&>(status) = InstanceIdent {"service1", "subject1", 0, UpdateItemTypeEnum::eService};
        status.mVersion                     = "1.0.0";
        status.mPreinstalled                = false;
        status.mRuntimeID                   = "runtime1";
        status.mManifestDigest              = "sha256:1234567890";
        status.mState                       = InstanceStateEnum::eActive;

        statuses->PushBack(status);

        return statuses;
    }

    testing::NiceMock<TLSCredentialsMock>                            mTLSCredentials;
    testing::NiceMock<aos::iamclient::CertProviderMock>              mCertProvider;
    testing::NiceMock<sm::launcher::RuntimeInfoProviderMock>         mRuntimeInfoProvider;
    testing::NiceMock<sm::resourcemanager::ResourceInfoProviderMock> mResourceInfoProvider;
    testing::NiceMock<sm::nodeconfig::NodeConfigHandlerMock>         mNodeConfigHandler;
    testing::NiceMock<sm::launcher::LauncherMock>                    mLauncher;
    testing::NiceMock<sm::logging::LogProviderMock>                  mLogProvider;
    testing::NiceMock<sm::networkmanager::NetworkManagerMock>        mNetworkManager;
    testing::NiceMock<monitoring::MonitoringMock>                    mMonitoring;
    testing::NiceMock<instancestatusprovider::ProviderMock>          mInstanceStatusProvider;
    testing::NiceMock<nodeconfig::JSONProviderMock>                  mJSONProvider;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(SMClientTest, RegisterSMSucceeds)
{
    auto server = std::make_unique<SMServiceStub>(GetConfig().mCMServerURL);
    auto client = std::make_unique<sm::smclient::SMClient>();

    auto runtimes  = CreateRuntimeInfos();
    auto resources = CreateResourceInfos();
    auto statuses  = CreateInstanceStatuses();

    EXPECT_CALL(mTLSCredentials, GetTLSClientCredentials(_))
        .WillRepeatedly(Return(aos::RetWithError<std::shared_ptr<grpc::ChannelCredentials>> {
            grpc::InsecureChannelCredentials(), aos::ErrorEnum::eNone}));
    EXPECT_CALL(mRuntimeInfoProvider, GetRuntimesInfos(_)).WillRepeatedly(Invoke([&runtimes](Array<RuntimeInfo>& out) {
        for (const auto& item : *runtimes) {
            out.PushBack(item);
        }
        return ErrorEnum::eNone;
    }));
    EXPECT_CALL(mResourceInfoProvider, GetResourcesInfos(_))
        .WillRepeatedly(Invoke([&resources](Array<sm::resourcemanager::ResourceInfo>& out) {
            for (const auto& item : *resources) {
                out.PushBack(item);
            }
            return ErrorEnum::eNone;
        }));
    EXPECT_CALL(mInstanceStatusProvider, GetInstancesStatuses(_))
        .WillRepeatedly(Invoke([&statuses](Array<InstanceStatus>& out) {
            for (const auto& item : *statuses) {
                out.PushBack(item);
            }
            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(*server, OnSMInfo(_)).WillOnce(Invoke([](const smproto::SMInfo& info) {
        EXPECT_EQ(info.node_id(), "test-node");
        EXPECT_EQ(info.runtimes_size(), 1);
        EXPECT_EQ(info.runtimes(0).runtime_id(), "runtime1");
        EXPECT_EQ(info.runtimes(0).type(), "runc");
        EXPECT_EQ(info.resources_size(), 1);
        EXPECT_EQ(info.resources(0).name(), "resource1");
    }));

    EXPECT_CALL(*server, OnNodeInstancesStatus(_)).WillOnce(Invoke([](const smproto::NodeInstancesStatus& status) {
        EXPECT_EQ(status.instances_size(), 1);
        EXPECT_EQ(status.instances(0).instance().item_id(), "service1");
        EXPECT_EQ(status.instances(0).version(), "1.0.0");
    }));

    auto err = client->Init(GetConfig(), "test-node", mTLSCredentials, mCertProvider, mRuntimeInfoProvider,
        mResourceInfoProvider, mNodeConfigHandler, mLauncher, mLogProvider, mNetworkManager, mMonitoring,
        mInstanceStatusProvider, mJSONProvider, false);
    ASSERT_TRUE(err.IsNone()) << "Init failed";

    err = client->Start();
    ASSERT_TRUE(err.IsNone()) << "Start failed";

    server->WaitRegistered();
    server->WaitSMInfo();
    server->WaitNodeInstancesStatus();

    err = client->Stop();
    ASSERT_TRUE(err.IsNone()) << "Stop failed";
}

TEST_F(SMClientTest, SendSMInfoWithMultipleRuntimesAndResources)
{
    auto server = std::make_unique<SMServiceStub>(GetConfig().mCMServerURL);
    auto client = std::make_unique<sm::smclient::SMClient>();

    auto runtimes = std::make_unique<RuntimeInfoArray>();
    for (int i = 0; i < 3; i++) {
        RuntimeInfo runtime;
        runtime.mRuntimeID   = (std::string("runtime") + std::to_string(i)).c_str();
        runtime.mRuntimeType = "runc";
        runtimes->PushBack(runtime);
    }

    auto resources = std::make_unique<StaticArray<sm::resourcemanager::ResourceInfo, 4>>();
    for (int i = 0; i < 2; i++) {
        sm::resourcemanager::ResourceInfo resource;
        resource.mName = (std::string("resource") + std::to_string(i)).c_str();
        resources->PushBack(resource);
    }

    auto statuses = CreateInstanceStatuses();

    EXPECT_CALL(mTLSCredentials, GetTLSClientCredentials(_))
        .WillRepeatedly(Return(aos::RetWithError<std::shared_ptr<grpc::ChannelCredentials>> {
            grpc::InsecureChannelCredentials(), aos::ErrorEnum::eNone}));
    EXPECT_CALL(mRuntimeInfoProvider, GetRuntimesInfos(_)).WillRepeatedly(Invoke([&runtimes](Array<RuntimeInfo>& out) {
        for (const auto& item : *runtimes) {
            out.PushBack(item);
        }
        return ErrorEnum::eNone;
    }));
    EXPECT_CALL(mResourceInfoProvider, GetResourcesInfos(_))
        .WillRepeatedly(Invoke([&resources](Array<sm::resourcemanager::ResourceInfo>& out) {
            for (const auto& item : *resources) {
                out.PushBack(item);
            }
            return ErrorEnum::eNone;
        }));
    EXPECT_CALL(mInstanceStatusProvider, GetInstancesStatuses(_))
        .WillRepeatedly(Invoke([&statuses](Array<InstanceStatus>& out) {
            for (const auto& item : *statuses) {
                out.PushBack(item);
            }
            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(*server, OnSMInfo(_)).WillOnce(Invoke([](const smproto::SMInfo& info) {
        EXPECT_EQ(info.runtimes_size(), 3);
        EXPECT_EQ(info.resources_size(), 2);
    }));

    EXPECT_CALL(*server, OnNodeInstancesStatus(_)).Times(1);

    auto err = client->Init(GetConfig(), "test-node", mTLSCredentials, mCertProvider, mRuntimeInfoProvider,
        mResourceInfoProvider, mNodeConfigHandler, mLauncher, mLogProvider, mNetworkManager, mMonitoring,
        mInstanceStatusProvider, mJSONProvider, false);
    ASSERT_TRUE(err.IsNone()) << "Init failed";

    err = client->Start();
    ASSERT_TRUE(err.IsNone()) << "Start failed";

    server->WaitSMInfo();
    server->WaitNodeInstancesStatus();

    err = client->Stop();
    ASSERT_TRUE(err.IsNone()) << "Stop failed";
}

TEST_F(SMClientTest, SendNodeInstancesStatusWithMultipleInstances)
{
    auto server = std::make_unique<SMServiceStub>(GetConfig().mCMServerURL);
    auto client = std::make_unique<sm::smclient::SMClient>();

    auto runtimes  = CreateRuntimeInfos();
    auto resources = CreateResourceInfos();

    auto statuses = std::make_unique<InstanceStatusArray>();
    for (int i = 0; i < 3; i++) {
        InstanceStatus status;
        static_cast<InstanceIdent&>(status) = InstanceIdent {(std::string("service") + std::to_string(i)).c_str(),
            "subject1", static_cast<uint64_t>(i), UpdateItemTypeEnum::eService};
        status.mVersion                     = "1.0.0";
        status.mRuntimeID                   = "runtime1";
        status.mState                       = InstanceStateEnum::eActive;
        statuses->PushBack(status);
    }

    EXPECT_CALL(mTLSCredentials, GetTLSClientCredentials(_))
        .WillRepeatedly(Return(aos::RetWithError<std::shared_ptr<grpc::ChannelCredentials>> {
            grpc::InsecureChannelCredentials(), aos::ErrorEnum::eNone}));
    EXPECT_CALL(mRuntimeInfoProvider, GetRuntimesInfos(_)).WillRepeatedly(Invoke([&runtimes](Array<RuntimeInfo>& out) {
        for (const auto& item : *runtimes) {
            out.PushBack(item);
        }
        return ErrorEnum::eNone;
    }));
    EXPECT_CALL(mResourceInfoProvider, GetResourcesInfos(_))
        .WillRepeatedly(Invoke([&resources](Array<sm::resourcemanager::ResourceInfo>& out) {
            for (const auto& item : *resources) {
                out.PushBack(item);
            }
            return ErrorEnum::eNone;
        }));
    EXPECT_CALL(mInstanceStatusProvider, GetInstancesStatuses(_))
        .WillRepeatedly(Invoke([&statuses](Array<InstanceStatus>& out) {
            for (const auto& item : *statuses) {
                out.PushBack(item);
            }
            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(*server, OnSMInfo(_)).Times(1);

    EXPECT_CALL(*server, OnNodeInstancesStatus(_)).WillOnce(Invoke([](const smproto::NodeInstancesStatus& status) {
        EXPECT_EQ(status.instances_size(), 3);
        EXPECT_EQ(status.instances(0).instance().item_id(), "service0");
        EXPECT_EQ(status.instances(1).instance().item_id(), "service1");
        EXPECT_EQ(status.instances(2).instance().item_id(), "service2");
    }));

    auto err = client->Init(GetConfig(), "test-node", mTLSCredentials, mCertProvider, mRuntimeInfoProvider,
        mResourceInfoProvider, mNodeConfigHandler, mLauncher, mLogProvider, mNetworkManager, mMonitoring,
        mInstanceStatusProvider, mJSONProvider, false);
    ASSERT_TRUE(err.IsNone()) << "Init failed";

    err = client->Start();
    ASSERT_TRUE(err.IsNone()) << "Start failed";

    server->WaitNodeInstancesStatus();

    err = client->Stop();
    ASSERT_TRUE(err.IsNone()) << "Stop failed";
}

TEST_F(SMClientTest, ClientNotStarted)
{
    auto server = std::make_unique<SMServiceStub>(GetConfig().mCMServerURL);
    auto client = std::make_unique<sm::smclient::SMClient>();

    auto err = client->Init(GetConfig(), "test-node", mTLSCredentials, mCertProvider, mRuntimeInfoProvider,
        mResourceInfoProvider, mNodeConfigHandler, mLauncher, mLogProvider, mNetworkManager, mMonitoring,
        mInstanceStatusProvider, mJSONProvider, false);
    ASSERT_TRUE(err.IsNone()) << "Init failed";

    EXPECT_CALL(*server, OnSMInfo(_)).Times(0);
    EXPECT_CALL(*server, OnNodeInstancesStatus(_)).Times(0);

    err = client->Stop();
    ASSERT_TRUE(err.IsNone()) << "Stop should return no error if start wasn't called";
}

TEST_F(SMClientTest, SecondStartReturnsError)
{
    auto server = std::make_unique<SMServiceStub>(GetConfig().mCMServerURL);
    auto client = std::make_unique<sm::smclient::SMClient>();

    auto runtimes  = CreateRuntimeInfos();
    auto resources = CreateResourceInfos();
    auto statuses  = CreateInstanceStatuses();

    EXPECT_CALL(mTLSCredentials, GetTLSClientCredentials(_))
        .WillRepeatedly(Return(aos::RetWithError<std::shared_ptr<grpc::ChannelCredentials>> {
            grpc::InsecureChannelCredentials(), aos::ErrorEnum::eNone}));
    EXPECT_CALL(mRuntimeInfoProvider, GetRuntimesInfos(_)).WillRepeatedly(Invoke([&runtimes](Array<RuntimeInfo>& out) {
        for (const auto& item : *runtimes) {
            out.PushBack(item);
        }
        return ErrorEnum::eNone;
    }));
    EXPECT_CALL(mResourceInfoProvider, GetResourcesInfos(_))
        .WillRepeatedly(Invoke([&resources](Array<sm::resourcemanager::ResourceInfo>& out) {
            for (const auto& item : *resources) {
                out.PushBack(item);
            }
            return ErrorEnum::eNone;
        }));
    EXPECT_CALL(mInstanceStatusProvider, GetInstancesStatuses(_))
        .WillRepeatedly(Invoke([&statuses](Array<InstanceStatus>& out) {
            for (const auto& item : *statuses) {
                out.PushBack(item);
            }
            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(*server, OnSMInfo(_)).Times(1);
    EXPECT_CALL(*server, OnNodeInstancesStatus(_)).Times(1);

    auto err = client->Init(GetConfig(), "test-node", mTLSCredentials, mCertProvider, mRuntimeInfoProvider,
        mResourceInfoProvider, mNodeConfigHandler, mLauncher, mLogProvider, mNetworkManager, mMonitoring,
        mInstanceStatusProvider, mJSONProvider, false);
    ASSERT_TRUE(err.IsNone()) << "Init failed";

    err = client->Start();
    ASSERT_TRUE(err.IsNone()) << "First Start failed";

    server->WaitRegistered();
    server->WaitSMInfo();
    server->WaitNodeInstancesStatus();

    err = client->Start();
    EXPECT_TRUE(err.Is(ErrorEnum::eFailed)) << "Second Start should fail";

    err = client->Stop();
    ASSERT_TRUE(err.IsNone()) << "Stop failed";
}

TEST_F(SMClientTest, SendNodeInstancesStatusesCallback)
{
    auto server = std::make_unique<SMServiceStub>(GetConfig().mCMServerURL);
    auto client = std::make_unique<sm::smclient::SMClient>();

    auto runtimes  = CreateRuntimeInfos();
    auto resources = CreateResourceInfos();
    auto statuses  = CreateInstanceStatuses();

    EXPECT_CALL(mTLSCredentials, GetTLSClientCredentials(_))
        .WillRepeatedly(Return(aos::RetWithError<std::shared_ptr<grpc::ChannelCredentials>> {
            grpc::InsecureChannelCredentials(), aos::ErrorEnum::eNone}));
    EXPECT_CALL(mRuntimeInfoProvider, GetRuntimesInfos(_)).WillRepeatedly(Invoke([&runtimes](Array<RuntimeInfo>& out) {
        for (const auto& item : *runtimes) {
            out.PushBack(item);
        }
        return ErrorEnum::eNone;
    }));
    EXPECT_CALL(mResourceInfoProvider, GetResourcesInfos(_))
        .WillRepeatedly(Invoke([&resources](Array<sm::resourcemanager::ResourceInfo>& out) {
            for (const auto& item : *resources) {
                out.PushBack(item);
            }
            return ErrorEnum::eNone;
        }));
    EXPECT_CALL(mInstanceStatusProvider, GetInstancesStatuses(_))
        .WillRepeatedly(Invoke([&statuses](Array<InstanceStatus>& out) {
            for (const auto& item : *statuses) {
                out.PushBack(item);
            }
            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(*server, OnSMInfo(_)).Times(1);
    EXPECT_CALL(*server, OnNodeInstancesStatus(_)).Times(AtLeast(1));

    auto err = client->Init(GetConfig(), "test-node", mTLSCredentials, mCertProvider, mRuntimeInfoProvider,
        mResourceInfoProvider, mNodeConfigHandler, mLauncher, mLogProvider, mNetworkManager, mMonitoring,
        mInstanceStatusProvider, mJSONProvider, false);
    ASSERT_TRUE(err.IsNone()) << "Init failed";

    err = client->Start();
    ASSERT_TRUE(err.IsNone()) << "Start failed";

    server->WaitRegistered();
    server->WaitSMInfo();
    server->WaitNodeInstancesStatus();

    InstanceStatusArray callbackStatuses;
    InstanceStatus      status;
    static_cast<InstanceIdent&>(status)
        = InstanceIdent {"callback-service", "subject1", 1, UpdateItemTypeEnum::eService};
    status.mVersion   = "2.0.0";
    status.mRuntimeID = "runtime1";
    status.mState     = InstanceStateEnum::eActive;
    callbackStatuses.PushBack(status);

    err = client->SendNodeInstancesStatuses(callbackStatuses);
    ASSERT_TRUE(err.IsNone()) << "SendNodeInstancesStatuses failed";

    err = client->Stop();
    ASSERT_TRUE(err.IsNone()) << "Stop failed";
}

TEST_F(SMClientTest, SendUpdateInstancesStatusesCallback)
{
    auto server = std::make_unique<SMServiceStub>(GetConfig().mCMServerURL);
    auto client = std::make_unique<sm::smclient::SMClient>();

    auto runtimes  = CreateRuntimeInfos();
    auto resources = CreateResourceInfos();
    auto statuses  = CreateInstanceStatuses();

    EXPECT_CALL(mTLSCredentials, GetTLSClientCredentials(_))
        .WillRepeatedly(Return(aos::RetWithError<std::shared_ptr<grpc::ChannelCredentials>> {
            grpc::InsecureChannelCredentials(), aos::ErrorEnum::eNone}));
    EXPECT_CALL(mRuntimeInfoProvider, GetRuntimesInfos(_)).WillRepeatedly(Invoke([&runtimes](Array<RuntimeInfo>& out) {
        for (const auto& item : *runtimes) {
            out.PushBack(item);
        }
        return ErrorEnum::eNone;
    }));
    EXPECT_CALL(mResourceInfoProvider, GetResourcesInfos(_))
        .WillRepeatedly(Invoke([&resources](Array<sm::resourcemanager::ResourceInfo>& out) {
            for (const auto& item : *resources) {
                out.PushBack(item);
            }
            return ErrorEnum::eNone;
        }));
    EXPECT_CALL(mInstanceStatusProvider, GetInstancesStatuses(_))
        .WillRepeatedly(Invoke([&statuses](Array<InstanceStatus>& out) {
            for (const auto& item : *statuses) {
                out.PushBack(item);
            }
            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(*server, OnSMInfo(_)).Times(1);
    EXPECT_CALL(*server, OnNodeInstancesStatus(_)).Times(1);
    EXPECT_CALL(*server, OnUpdateInstancesStatus(_)).Times(1);

    auto err = client->Init(GetConfig(), "test-node", mTLSCredentials, mCertProvider, mRuntimeInfoProvider,
        mResourceInfoProvider, mNodeConfigHandler, mLauncher, mLogProvider, mNetworkManager, mMonitoring,
        mInstanceStatusProvider, mJSONProvider, false);
    ASSERT_TRUE(err.IsNone()) << "Init failed";

    err = client->Start();
    ASSERT_TRUE(err.IsNone()) << "Start failed";

    server->WaitRegistered();
    server->WaitSMInfo();
    server->WaitNodeInstancesStatus();

    InstanceStatusArray updateStatuses;
    InstanceStatus      status;
    static_cast<InstanceIdent&>(status) = InstanceIdent {"update-service", "subject1", 2, UpdateItemTypeEnum::eService};
    status.mVersion                     = "3.0.0";
    status.mRuntimeID                   = "runtime1";
    status.mState                       = InstanceStateEnum::eActive;
    updateStatuses.PushBack(status);

    client->SendUpdateInstancesStatuses(updateStatuses);

    server->WaitUpdateInstancesStatus();

    err = client->Stop();
    ASSERT_TRUE(err.IsNone()) << "Stop failed";
}

TEST_F(SMClientTest, SendMonitoringData)
{
    auto server = std::make_unique<SMServiceStub>(GetConfig().mCMServerURL);
    auto client = std::make_unique<sm::smclient::SMClient>();

    auto runtimes  = CreateRuntimeInfos();
    auto resources = CreateResourceInfos();
    auto statuses  = CreateInstanceStatuses();

    EXPECT_CALL(mTLSCredentials, GetTLSClientCredentials(_))
        .WillRepeatedly(Return(aos::RetWithError<std::shared_ptr<grpc::ChannelCredentials>> {
            grpc::InsecureChannelCredentials(), aos::ErrorEnum::eNone}));
    EXPECT_CALL(mRuntimeInfoProvider, GetRuntimesInfos(_)).WillRepeatedly(Invoke([&runtimes](Array<RuntimeInfo>& out) {
        for (const auto& item : *runtimes) {
            out.PushBack(item);
        }
        return ErrorEnum::eNone;
    }));
    EXPECT_CALL(mResourceInfoProvider, GetResourcesInfos(_))
        .WillRepeatedly(Invoke([&resources](Array<sm::resourcemanager::ResourceInfo>& out) {
            for (const auto& item : *resources) {
                out.PushBack(item);
            }
            return ErrorEnum::eNone;
        }));
    EXPECT_CALL(mInstanceStatusProvider, GetInstancesStatuses(_))
        .WillRepeatedly(Invoke([&statuses](Array<InstanceStatus>& out) {
            for (const auto& item : *statuses) {
                out.PushBack(item);
            }
            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(*server, OnSMInfo(_)).Times(1);
    EXPECT_CALL(*server, OnNodeInstancesStatus(_)).Times(1);
    EXPECT_CALL(*server, OnInstantMonitoring(_)).WillOnce(Invoke([](const smproto::InstantMonitoring& monitoring) {
        EXPECT_TRUE(monitoring.has_node_monitoring());
        EXPECT_EQ(monitoring.instances_monitoring_size(), 2);
        EXPECT_EQ(monitoring.instances_monitoring(0).instance().item_id(), "service1");
        EXPECT_EQ(monitoring.instances_monitoring(0).runtime_id(), "runtime1");
        EXPECT_EQ(monitoring.instances_monitoring(1).instance().item_id(), "service2");
        EXPECT_EQ(monitoring.instances_monitoring(1).runtime_id(), "runtime2");
    }));

    auto err = client->Init(GetConfig(), "test-node", mTLSCredentials, mCertProvider, mRuntimeInfoProvider,
        mResourceInfoProvider, mNodeConfigHandler, mLauncher, mLogProvider, mNetworkManager, mMonitoring,
        mInstanceStatusProvider, mJSONProvider, false);
    ASSERT_TRUE(err.IsNone()) << "Init failed";

    err = client->Start();
    ASSERT_TRUE(err.IsNone()) << "Start failed";

    server->WaitRegistered();
    server->WaitSMInfo();
    server->WaitNodeInstancesStatus();

    aos::monitoring::NodeMonitoringData monitoringData;
    monitoringData.mTimestamp = Time::Now();
    monitoringData.mNodeID    = "test-node";

    monitoringData.mMonitoringData.mTimestamp = monitoringData.mTimestamp;
    monitoringData.mMonitoringData.mRAM       = 1024 * 1024 * 512; // 512 MB
    monitoringData.mMonitoringData.mCPU       = 50.5;
    monitoringData.mMonitoringData.mDownload  = 1000;
    monitoringData.mMonitoringData.mUpload    = 500;

    aos::monitoring::InstanceMonitoringData instance1;
    instance1.mInstanceIdent  = InstanceIdent {"service1", "subject1", 0, UpdateItemTypeEnum::eService};
    instance1.mRuntimeID      = "runtime1";
    instance1.mMonitoringData = monitoringData.mMonitoringData;
    monitoringData.mInstances.PushBack(instance1);

    aos::monitoring::InstanceMonitoringData instance2;
    instance2.mInstanceIdent  = InstanceIdent {"service2", "subject1", 1, UpdateItemTypeEnum::eService};
    instance2.mRuntimeID      = "runtime2";
    instance2.mMonitoringData = monitoringData.mMonitoringData;
    monitoringData.mInstances.PushBack(instance2);

    err = client->SendMonitoringData(monitoringData);
    ASSERT_TRUE(err.IsNone()) << "SendMonitoringData failed";

    server->WaitInstantMonitoring();

    err = client->Stop();
    ASSERT_TRUE(err.IsNone()) << "Stop failed";
}

TEST_F(SMClientTest, SendAlert)
{
    auto server = std::make_unique<SMServiceStub>(GetConfig().mCMServerURL);
    auto client = std::make_unique<sm::smclient::SMClient>();

    auto runtimes  = CreateRuntimeInfos();
    auto resources = CreateResourceInfos();
    auto statuses  = CreateInstanceStatuses();

    EXPECT_CALL(mTLSCredentials, GetTLSClientCredentials(_))
        .WillRepeatedly(Return(aos::RetWithError<std::shared_ptr<grpc::ChannelCredentials>> {
            grpc::InsecureChannelCredentials(), aos::ErrorEnum::eNone}));
    EXPECT_CALL(mRuntimeInfoProvider, GetRuntimesInfos(_)).WillRepeatedly(Invoke([&runtimes](Array<RuntimeInfo>& out) {
        for (const auto& item : *runtimes) {
            out.PushBack(item);
        }
        return ErrorEnum::eNone;
    }));
    EXPECT_CALL(mResourceInfoProvider, GetResourcesInfos(_))
        .WillRepeatedly(Invoke([&resources](Array<sm::resourcemanager::ResourceInfo>& out) {
            for (const auto& item : *resources) {
                out.PushBack(item);
            }
            return ErrorEnum::eNone;
        }));
    EXPECT_CALL(mInstanceStatusProvider, GetInstancesStatuses(_))
        .WillRepeatedly(Invoke([&statuses](Array<InstanceStatus>& out) {
            for (const auto& item : *statuses) {
                out.PushBack(item);
            }
            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(*server, OnSMInfo(_)).Times(1);
    EXPECT_CALL(*server, OnNodeInstancesStatus(_)).Times(1);

    EXPECT_CALL(*server, OnAlert(_))
        .Times(6)
        .WillOnce(Invoke([](const smproto::Alert& alert) {
            // SystemAlert
            EXPECT_TRUE(alert.has_timestamp());
            EXPECT_TRUE(alert.has_system_alert());
            EXPECT_EQ(alert.system_alert().message(), "System alert message");
        }))
        .WillOnce(Invoke([](const smproto::Alert& alert) {
            // CoreAlert
            EXPECT_TRUE(alert.has_timestamp());
            EXPECT_TRUE(alert.has_core_alert());
            EXPECT_EQ(alert.core_alert().core_component(), "SM");
            EXPECT_EQ(alert.core_alert().message(), "Core alert message");
        }))
        .WillOnce(Invoke([](const smproto::Alert& alert) {
            // SystemQuotaAlert
            EXPECT_TRUE(alert.has_timestamp());
            EXPECT_TRUE(alert.has_system_quota_alert());
            EXPECT_EQ(alert.system_quota_alert().parameter(), "ram");
            EXPECT_EQ(alert.system_quota_alert().value(), 1024);
            EXPECT_EQ(alert.system_quota_alert().status(), "raise");
        }))
        .WillOnce(Invoke([](const smproto::Alert& alert) {
            // InstanceQuotaAlert
            EXPECT_TRUE(alert.has_timestamp());
            EXPECT_TRUE(alert.has_instance_quota_alert());
            EXPECT_EQ(alert.instance_quota_alert().instance().item_id(), "service1");
            EXPECT_EQ(alert.instance_quota_alert().parameter(), "cpu");
            EXPECT_EQ(alert.instance_quota_alert().value(), 90);
            EXPECT_EQ(alert.instance_quota_alert().status(), "raise");
        }))
        .WillOnce(Invoke([](const smproto::Alert& alert) {
            // ResourceAllocateAlert
            EXPECT_TRUE(alert.has_timestamp());
            EXPECT_TRUE(alert.has_resource_allocate_alert());
            EXPECT_EQ(alert.resource_allocate_alert().instance().item_id(), "service1");
            EXPECT_EQ(alert.resource_allocate_alert().resource(), "gpu");
            EXPECT_EQ(alert.resource_allocate_alert().message(), "Resource allocation failed");
        }))
        .WillOnce(Invoke([](const smproto::Alert& alert) {
            // InstanceAlert
            EXPECT_TRUE(alert.has_timestamp());
            EXPECT_TRUE(alert.has_instance_alert());
            EXPECT_EQ(alert.instance_alert().instance().item_id(), "service1");
            EXPECT_EQ(alert.instance_alert().service_version(), "1.0.0");
            EXPECT_EQ(alert.instance_alert().message(), "Instance alert message");
        }));

    auto err = client->Init(GetConfig(), "test-node", mTLSCredentials, mCertProvider, mRuntimeInfoProvider,
        mResourceInfoProvider, mNodeConfigHandler, mLauncher, mLogProvider, mNetworkManager, mMonitoring,
        mInstanceStatusProvider, mJSONProvider, false);
    ASSERT_TRUE(err.IsNone()) << "Init failed";

    err = client->Start();
    ASSERT_TRUE(err.IsNone()) << "Start failed";

    server->WaitRegistered();
    server->WaitSMInfo();
    server->WaitNodeInstancesStatus();

    // Send SystemAlert
    {
        SystemAlert alert;
        alert.mTimestamp = Time::Now();
        alert.mNodeID    = "test-node";
        alert.mMessage   = "System alert message";

        err = client->SendAlert(AlertVariant(alert));
        ASSERT_TRUE(err.IsNone()) << "SendAlert(SystemAlert) failed";
        server->WaitAlert();
    }

    // Send CoreAlert
    {
        CoreAlert alert;
        alert.mTimestamp     = Time::Now();
        alert.mNodeID        = "test-node";
        alert.mCoreComponent = CoreComponentEnum::eSM;
        alert.mMessage       = "Core alert message";

        err = client->SendAlert(AlertVariant(alert));
        ASSERT_TRUE(err.IsNone()) << "SendAlert(CoreAlert) failed";
        server->WaitAlert();
    }

    // Send SystemQuotaAlert
    {
        SystemQuotaAlert alert;
        alert.mTimestamp = Time::Now();
        alert.mNodeID    = "test-node";
        alert.mParameter = "ram";
        alert.mValue     = 1024;
        alert.mState     = QuotaAlertStateEnum::eRaise;

        err = client->SendAlert(AlertVariant(alert));
        ASSERT_TRUE(err.IsNone()) << "SendAlert(SystemQuotaAlert) failed";
        server->WaitAlert();
    }

    // Send InstanceQuotaAlert
    {
        InstanceQuotaAlert alert;
        alert.mTimestamp                   = Time::Now();
        static_cast<InstanceIdent&>(alert) = InstanceIdent {"service1", "subject1", 0, UpdateItemTypeEnum::eService};
        alert.mParameter                   = "cpu";
        alert.mValue                       = 90;
        alert.mState                       = QuotaAlertStateEnum::eRaise;

        err = client->SendAlert(AlertVariant(alert));
        ASSERT_TRUE(err.IsNone()) << "SendAlert(InstanceQuotaAlert) failed";
        server->WaitAlert();
    }

    // Send ResourceAllocateAlert
    {
        ResourceAllocateAlert alert;
        alert.mTimestamp                   = Time::Now();
        alert.mNodeID                      = "test-node";
        static_cast<InstanceIdent&>(alert) = InstanceIdent {"service1", "subject1", 0, UpdateItemTypeEnum::eService};
        alert.mResource                    = "gpu";
        alert.mMessage                     = "Resource allocation failed";

        err = client->SendAlert(AlertVariant(alert));
        ASSERT_TRUE(err.IsNone()) << "SendAlert(ResourceAllocateAlert) failed";
        server->WaitAlert();
    }

    // Send InstanceAlert
    {
        InstanceAlert alert;
        alert.mTimestamp                   = Time::Now();
        static_cast<InstanceIdent&>(alert) = InstanceIdent {"service1", "subject1", 0, UpdateItemTypeEnum::eService};
        alert.mVersion                     = "1.0.0";
        alert.mMessage                     = "Instance alert message";

        err = client->SendAlert(AlertVariant(alert));
        ASSERT_TRUE(err.IsNone()) << "SendAlert(InstanceAlert) failed";
        server->WaitAlert();
    }

    err = client->Stop();
    ASSERT_TRUE(err.IsNone()) << "Stop failed";
}

TEST_F(SMClientTest, GetBlobsInfo)
{
    auto server = std::make_unique<SMServiceStub>(GetConfig().mCMServerURL);
    auto client = std::make_unique<sm::smclient::SMClient>();

    auto runtimes  = CreateRuntimeInfos();
    auto resources = CreateResourceInfos();
    auto statuses  = CreateInstanceStatuses();

    EXPECT_CALL(mTLSCredentials, GetTLSClientCredentials(_))
        .WillRepeatedly(Return(aos::RetWithError<std::shared_ptr<grpc::ChannelCredentials>> {
            grpc::InsecureChannelCredentials(), aos::ErrorEnum::eNone}));
    EXPECT_CALL(mRuntimeInfoProvider, GetRuntimesInfos(_)).WillRepeatedly(Invoke([&runtimes](Array<RuntimeInfo>& out) {
        for (const auto& item : *runtimes) {
            out.PushBack(item);
        }
        return ErrorEnum::eNone;
    }));
    EXPECT_CALL(mResourceInfoProvider, GetResourcesInfos(_))
        .WillRepeatedly(Invoke([&resources](Array<sm::resourcemanager::ResourceInfo>& out) {
            for (const auto& item : *resources) {
                out.PushBack(item);
            }
            return ErrorEnum::eNone;
        }));
    EXPECT_CALL(mInstanceStatusProvider, GetInstancesStatuses(_))
        .WillRepeatedly(Invoke([&statuses](Array<InstanceStatus>& out) {
            for (const auto& item : *statuses) {
                out.PushBack(item);
            }
            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(*server, OnSMInfo(_)).Times(1);
    EXPECT_CALL(*server, OnNodeInstancesStatus(_)).Times(1);

    auto err = client->Init(GetConfig(), "test-node", mTLSCredentials, mCertProvider, mRuntimeInfoProvider,
        mResourceInfoProvider, mNodeConfigHandler, mLauncher, mLogProvider, mNetworkManager, mMonitoring,
        mInstanceStatusProvider, mJSONProvider, false);
    ASSERT_TRUE(err.IsNone()) << "Init failed";

    err = client->Start();
    ASSERT_TRUE(err.IsNone()) << "Start failed";

    server->WaitRegistered();
    server->WaitSMInfo();
    server->WaitNodeInstancesStatus();

    StaticArray<StaticString<oci::cDigestLen>, 2> digests;
    digests.EmplaceBack("sha256:1234567890abcdef");
    digests.EmplaceBack("sha256:fedcba0987654321");

    StaticArray<StaticString<cURLLen>, 2> urls;

    err = client->GetBlobsInfo(digests, urls);
    ASSERT_TRUE(err.IsNone()) << "GetBlobsInfo failed";

    ASSERT_EQ(urls.Size(), 2);
    EXPECT_EQ(urls[0], "http://example.com/blobs/sha256:1234567890abcdef");
    EXPECT_EQ(urls[1], "http://example.com/blobs/sha256:fedcba0987654321");

    err = client->Stop();
    ASSERT_TRUE(err.IsNone()) << "Stop failed";
}

TEST_F(SMClientTest, ProcessGetNodeConfigStatus)
{
    auto server = std::make_unique<SMServiceStub>(GetConfig().mCMServerURL);
    auto client = std::make_unique<sm::smclient::SMClient>();

    auto runtimes  = CreateRuntimeInfos();
    auto resources = CreateResourceInfos();
    auto statuses  = CreateInstanceStatuses();

    EXPECT_CALL(mTLSCredentials, GetTLSClientCredentials(_))
        .WillRepeatedly(Return(aos::RetWithError<std::shared_ptr<grpc::ChannelCredentials>> {
            grpc::InsecureChannelCredentials(), aos::ErrorEnum::eNone}));
    EXPECT_CALL(mRuntimeInfoProvider, GetRuntimesInfos(_)).WillRepeatedly(Invoke([&runtimes](Array<RuntimeInfo>& out) {
        for (const auto& item : *runtimes) {
            out.PushBack(item);
        }
        return ErrorEnum::eNone;
    }));
    EXPECT_CALL(mResourceInfoProvider, GetResourcesInfos(_))
        .WillRepeatedly(Invoke([&resources](Array<sm::resourcemanager::ResourceInfo>& out) {
            for (const auto& item : *resources) {
                out.PushBack(item);
            }
            return ErrorEnum::eNone;
        }));
    EXPECT_CALL(mInstanceStatusProvider, GetInstancesStatuses(_))
        .WillRepeatedly(Invoke([&statuses](Array<InstanceStatus>& out) {
            for (const auto& item : *statuses) {
                out.PushBack(item);
            }
            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(mNodeConfigHandler, GetNodeConfigStatus(_)).WillOnce(Invoke([](NodeConfigStatus& status) {
        status.mState   = UnitConfigStateEnum::eInstalled;
        status.mVersion = "1.0.0";
        return ErrorEnum::eNone;
    }));

    EXPECT_CALL(*server, OnSMInfo(_)).Times(1);
    EXPECT_CALL(*server, OnNodeInstancesStatus(_)).Times(1);
    EXPECT_CALL(*server, OnNodeConfigStatus(_)).WillOnce(Invoke([](const smproto::NodeConfigStatus& status) {
        EXPECT_EQ(status.state(), "installed");
        EXPECT_EQ(status.version(), "1.0.0");
    }));

    auto err = client->Init(GetConfig(), "test-node", mTLSCredentials, mCertProvider, mRuntimeInfoProvider,
        mResourceInfoProvider, mNodeConfigHandler, mLauncher, mLogProvider, mNetworkManager, mMonitoring,
        mInstanceStatusProvider, mJSONProvider, false);
    ASSERT_TRUE(err.IsNone()) << "Init failed";

    err = client->Start();
    ASSERT_TRUE(err.IsNone()) << "Start failed";

    server->WaitRegistered();
    server->WaitSMInfo();
    server->WaitNodeInstancesStatus();

    server->SendGetNodeConfigStatus();
    server->WaitNodeConfigStatus();

    err = client->Stop();
    ASSERT_TRUE(err.IsNone()) << "Stop failed";
}

TEST_F(SMClientTest, ProcessUpdateInstances)
{
    auto server = std::make_unique<SMServiceStub>(GetConfig().mCMServerURL);
    auto client = std::make_unique<sm::smclient::SMClient>();

    auto runtimes  = CreateRuntimeInfos();
    auto resources = CreateResourceInfos();
    auto statuses  = CreateInstanceStatuses();

    EXPECT_CALL(mTLSCredentials, GetTLSClientCredentials(_))
        .WillRepeatedly(Return(aos::RetWithError<std::shared_ptr<grpc::ChannelCredentials>> {
            grpc::InsecureChannelCredentials(), aos::ErrorEnum::eNone}));
    EXPECT_CALL(mRuntimeInfoProvider, GetRuntimesInfos(_)).WillRepeatedly(Invoke([&runtimes](Array<RuntimeInfo>& out) {
        for (const auto& item : *runtimes) {
            out.PushBack(item);
        }
        return ErrorEnum::eNone;
    }));
    EXPECT_CALL(mResourceInfoProvider, GetResourcesInfos(_))
        .WillRepeatedly(Invoke([&resources](Array<sm::resourcemanager::ResourceInfo>& out) {
            for (const auto& item : *resources) {
                out.PushBack(item);
            }
            return ErrorEnum::eNone;
        }));
    EXPECT_CALL(mInstanceStatusProvider, GetInstancesStatuses(_))
        .WillRepeatedly(Invoke([&statuses](Array<InstanceStatus>& out) {
            for (const auto& item : *statuses) {
                out.PushBack(item);
            }
            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(mLauncher, UpdateInstances(_, _))
        .WillOnce(Invoke([](const Array<InstanceIdent>& stopInstances, const Array<InstanceInfo>& startInstances) {
            EXPECT_EQ(stopInstances.Size(), 1);
            EXPECT_EQ(stopInstances[0].mItemID, "stop-service");

            EXPECT_EQ(startInstances.Size(), 1);
            EXPECT_EQ(startInstances[0].mItemID, "start-service");

            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(*server, OnSMInfo(_)).Times(1);
    EXPECT_CALL(*server, OnNodeInstancesStatus(_)).Times(1);

    auto err = client->Init(GetConfig(), "test-node", mTLSCredentials, mCertProvider, mRuntimeInfoProvider,
        mResourceInfoProvider, mNodeConfigHandler, mLauncher, mLogProvider, mNetworkManager, mMonitoring,
        mInstanceStatusProvider, mJSONProvider, false);
    ASSERT_TRUE(err.IsNone()) << "Init failed";

    err = client->Start();
    ASSERT_TRUE(err.IsNone()) << "Start failed";

    server->WaitRegistered();
    server->WaitSMInfo();
    server->WaitNodeInstancesStatus();

    smproto::InstanceInfo startInstance;
    startInstance.mutable_instance()->set_item_id("start-service");
    startInstance.mutable_instance()->set_subject_id("subject1");
    startInstance.mutable_instance()->set_instance(0);

    server->SendUpdateInstances({startInstance}, {"stop-service"});

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    err = client->Stop();
    ASSERT_TRUE(err.IsNone()) << "Stop failed";
}

TEST_F(SMClientTest, ProcessGetAverageMonitoring)
{
    auto server = std::make_unique<SMServiceStub>(GetConfig().mCMServerURL);
    auto client = std::make_unique<sm::smclient::SMClient>();

    auto runtimes  = CreateRuntimeInfos();
    auto resources = CreateResourceInfos();
    auto statuses  = CreateInstanceStatuses();

    EXPECT_CALL(mTLSCredentials, GetTLSClientCredentials(_))
        .WillRepeatedly(Return(aos::RetWithError<std::shared_ptr<grpc::ChannelCredentials>> {
            grpc::InsecureChannelCredentials(), aos::ErrorEnum::eNone}));
    EXPECT_CALL(mRuntimeInfoProvider, GetRuntimesInfos(_)).WillRepeatedly(Invoke([&runtimes](Array<RuntimeInfo>& out) {
        for (const auto& item : *runtimes) {
            out.PushBack(item);
        }
        return ErrorEnum::eNone;
    }));
    EXPECT_CALL(mResourceInfoProvider, GetResourcesInfos(_))
        .WillRepeatedly(Invoke([&resources](Array<sm::resourcemanager::ResourceInfo>& out) {
            for (const auto& item : *resources) {
                out.PushBack(item);
            }
            return ErrorEnum::eNone;
        }));
    EXPECT_CALL(mInstanceStatusProvider, GetInstancesStatuses(_))
        .WillRepeatedly(Invoke([&statuses](Array<InstanceStatus>& out) {
            for (const auto& item : *statuses) {
                out.PushBack(item);
            }
            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(mMonitoring, GetAverageMonitoringData(_))
        .WillOnce(Invoke([](aos::monitoring::NodeMonitoringData& data) {
            data.mNodeID                   = "test-node";
            data.mMonitoringData.mRAM      = 512 * 1024 * 1024;
            data.mMonitoringData.mCPU      = 25;
            data.mMonitoringData.mDownload = 1000;
            data.mMonitoringData.mUpload   = 500;
            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(*server, OnSMInfo(_)).Times(1);
    EXPECT_CALL(*server, OnNodeInstancesStatus(_)).Times(1);
    EXPECT_CALL(*server, OnAverageMonitoring(_)).WillOnce(Invoke([](const smproto::AverageMonitoring& monitoring) {
        EXPECT_TRUE(monitoring.has_node_monitoring());
        EXPECT_EQ(monitoring.node_monitoring().ram(), 512 * 1024 * 1024);
        EXPECT_EQ(monitoring.node_monitoring().cpu(), 25);
    }));

    auto err = client->Init(GetConfig(), "test-node", mTLSCredentials, mCertProvider, mRuntimeInfoProvider,
        mResourceInfoProvider, mNodeConfigHandler, mLauncher, mLogProvider, mNetworkManager, mMonitoring,
        mInstanceStatusProvider, mJSONProvider, false);
    ASSERT_TRUE(err.IsNone()) << "Init failed";

    err = client->Start();
    ASSERT_TRUE(err.IsNone()) << "Start failed";

    server->WaitRegistered();
    server->WaitSMInfo();
    server->WaitNodeInstancesStatus();

    server->SendGetAverageMonitoring();
    server->WaitAverageMonitoring();

    err = client->Stop();
    ASSERT_TRUE(err.IsNone()) << "Stop failed";
}

TEST_F(SMClientTest, ProcessSystemLogRequest)
{
    auto server = std::make_unique<SMServiceStub>(GetConfig().mCMServerURL);
    auto client = std::make_unique<sm::smclient::SMClient>();

    auto runtimes  = CreateRuntimeInfos();
    auto resources = CreateResourceInfos();
    auto statuses  = CreateInstanceStatuses();

    EXPECT_CALL(mTLSCredentials, GetTLSClientCredentials(_))
        .WillRepeatedly(Return(aos::RetWithError<std::shared_ptr<grpc::ChannelCredentials>> {
            grpc::InsecureChannelCredentials(), aos::ErrorEnum::eNone}));
    EXPECT_CALL(mRuntimeInfoProvider, GetRuntimesInfos(_)).WillRepeatedly(Invoke([&runtimes](Array<RuntimeInfo>& out) {
        for (const auto& item : *runtimes) {
            out.PushBack(item);
        }
        return ErrorEnum::eNone;
    }));
    EXPECT_CALL(mResourceInfoProvider, GetResourcesInfos(_))
        .WillRepeatedly(Invoke([&resources](Array<sm::resourcemanager::ResourceInfo>& out) {
            for (const auto& item : *resources) {
                out.PushBack(item);
            }
            return ErrorEnum::eNone;
        }));
    EXPECT_CALL(mInstanceStatusProvider, GetInstancesStatuses(_))
        .WillRepeatedly(Invoke([&statuses](Array<InstanceStatus>& out) {
            for (const auto& item : *statuses) {
                out.PushBack(item);
            }
            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(mLogProvider, GetSystemLog(_)).WillOnce(Invoke([](const RequestLog& request) {
        EXPECT_EQ(request.mCorrelationID, "correlation-123");
        EXPECT_EQ(request.mLogType, LogTypeEnum::eSystemLog);
        return ErrorEnum::eNone;
    }));

    EXPECT_CALL(*server, OnSMInfo(_)).Times(1);
    EXPECT_CALL(*server, OnNodeInstancesStatus(_)).Times(1);

    auto err = client->Init(GetConfig(), "test-node", mTLSCredentials, mCertProvider, mRuntimeInfoProvider,
        mResourceInfoProvider, mNodeConfigHandler, mLauncher, mLogProvider, mNetworkManager, mMonitoring,
        mInstanceStatusProvider, mJSONProvider, false);
    ASSERT_TRUE(err.IsNone()) << "Init failed";

    err = client->Start();
    ASSERT_TRUE(err.IsNone()) << "Start failed";

    server->WaitRegistered();
    server->WaitSMInfo();
    server->WaitNodeInstancesStatus();

    server->SendSystemLogRequest("correlation-123");

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    err = client->Stop();
    ASSERT_TRUE(err.IsNone()) << "Stop failed";
}

TEST_F(SMClientTest, ProcessUpdateNetworks)
{
    auto server = std::make_unique<SMServiceStub>(GetConfig().mCMServerURL);
    auto client = std::make_unique<sm::smclient::SMClient>();

    auto runtimes  = CreateRuntimeInfos();
    auto resources = CreateResourceInfos();
    auto statuses  = CreateInstanceStatuses();

    EXPECT_CALL(mTLSCredentials, GetTLSClientCredentials(_))
        .WillRepeatedly(Return(aos::RetWithError<std::shared_ptr<grpc::ChannelCredentials>> {
            grpc::InsecureChannelCredentials(), aos::ErrorEnum::eNone}));
    EXPECT_CALL(mRuntimeInfoProvider, GetRuntimesInfos(_)).WillRepeatedly(Invoke([&runtimes](Array<RuntimeInfo>& out) {
        for (const auto& item : *runtimes) {
            out.PushBack(item);
        }
        return ErrorEnum::eNone;
    }));
    EXPECT_CALL(mResourceInfoProvider, GetResourcesInfos(_))
        .WillRepeatedly(Invoke([&resources](Array<sm::resourcemanager::ResourceInfo>& out) {
            for (const auto& item : *resources) {
                out.PushBack(item);
            }
            return ErrorEnum::eNone;
        }));
    EXPECT_CALL(mInstanceStatusProvider, GetInstancesStatuses(_))
        .WillRepeatedly(Invoke([&statuses](Array<InstanceStatus>& out) {
            for (const auto& item : *statuses) {
                out.PushBack(item);
            }
            return ErrorEnum::eNone;
        }));

    EXPECT_CALL(mNetworkManager, UpdateNetworks(_)).WillOnce(Invoke([](const Array<NetworkParameters>& networks) {
        EXPECT_EQ(networks.Size(), 1);
        EXPECT_EQ(networks[0].mNetworkID, "network1");
        EXPECT_EQ(networks[0].mSubnet, "192.168.1.0/24");
        EXPECT_EQ(networks[0].mIP, "192.168.1.1");
        EXPECT_EQ(networks[0].mVlanID, 100);
        return ErrorEnum::eNone;
    }));

    EXPECT_CALL(*server, OnSMInfo(_)).Times(1);
    EXPECT_CALL(*server, OnNodeInstancesStatus(_)).Times(1);

    auto err = client->Init(GetConfig(), "test-node", mTLSCredentials, mCertProvider, mRuntimeInfoProvider,
        mResourceInfoProvider, mNodeConfigHandler, mLauncher, mLogProvider, mNetworkManager, mMonitoring,
        mInstanceStatusProvider, mJSONProvider, false);
    ASSERT_TRUE(err.IsNone()) << "Init failed";

    err = client->Start();
    ASSERT_TRUE(err.IsNone()) << "Start failed";

    server->WaitRegistered();
    server->WaitSMInfo();
    server->WaitNodeInstancesStatus();

    smproto::UpdateNetworkParameters network;
    network.set_network_id("network1");
    network.set_subnet("192.168.1.0/24");
    network.set_ip("192.168.1.1");
    network.set_vlan_id(100);

    server->SendUpdateNetworks({network});

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    err = client->Stop();
    ASSERT_TRUE(err.IsNone()) << "Stop failed";
}
