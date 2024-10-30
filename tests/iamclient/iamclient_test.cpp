/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gmock/gmock.h>

#include <aos/test/log.hpp>
#include <google/protobuf/util/message_differencer.h>
#include <grpcpp/server_builder.h>
#include <iamanager/v5/iamanager.grpc.pb.h>

#include "iamclient/iamclient.hpp"

#include "mocks/certhandlermock.hpp"
#include "mocks/certloadermock.hpp"
#include "mocks/identhandlermock.hpp"
#include "mocks/nodeinfoprovidermock.hpp"
#include "mocks/provisionmanagermock.hpp"
#include "mocks/x509providermock.hpp"

using namespace testing;
using namespace aos;

/***********************************************************************************************************************
 * Test utils
 **********************************************************************************************************************/

namespace common::v1 {

inline bool operator==(const ErrorInfo& left, const ErrorInfo& right)
{
    return google::protobuf::util::MessageDifferencer::Equals(left, right);
}

} // namespace common::v1

namespace iamanager::v5 {

inline bool operator==(const iamanager::v5::NodeInfo& left, const iamanager::v5::NodeInfo& right)
{
    return google::protobuf::util::MessageDifferencer::Equals(left, right);
}

} // namespace iamanager::v5

template <typename T1, typename T2>
void FillArray(const std::initializer_list<T1>& src, aos::Array<T2>& dst)
{
    for (const auto& val : src) {
        ASSERT_TRUE(dst.PushBack(val).IsNone());
    }
}

template <typename T1, typename T2>
void FillArray(const std::initializer_list<T1>& src, google::protobuf::RepeatedPtrField<T2>& dst)
{
    for (const auto& val : src) {
        *dst.Add() = val;
    }
}

template <typename T>
std::vector<T> ConvertFromProtoArray(const google::protobuf::RepeatedPtrField<T>& src)
{
    std::vector<T> dst;

    for (const auto& val : src) {
        dst.push_back(val);
    }

    return dst;
}

static CPUInfo CreateCPUInfo()
{
    CPUInfo cpuInfo;

    cpuInfo.mModelName  = "11th Gen Intel(R) Core(TM) i7-1185G7 @ 3.00GHz";
    cpuInfo.mNumCores   = 4;
    cpuInfo.mNumThreads = 4;
    cpuInfo.mArch       = "GenuineIntel";
    cpuInfo.mArchFamily = "6";

    return cpuInfo;
}

static PartitionInfo CreatePartitionInfo(const char* name, const std::initializer_list<const char*> types)
{
    PartitionInfo partitionInfo;

    partitionInfo.mName = name;
    FillArray(types, partitionInfo.mTypes);
    partitionInfo.mTotalSize = 16169908;
    partitionInfo.mPath      = "/sys/kernel/tracing";
    partitionInfo.mUsedSize  = 64156;

    return partitionInfo;
}

static NodeAttribute CreateAttribute(const char* name, const char* value)
{
    NodeAttribute attribute;

    attribute.mName  = name;
    attribute.mValue = value;

    return attribute;
}

static NodeInfo DefaultNodeInfo(NodeStatus status = NodeStatusEnum::eProvisioned)
{
    NodeInfo nodeInfo;

    nodeInfo.mNodeID   = "node0";
    nodeInfo.mNodeType = "main";
    nodeInfo.mName     = "node0";
    nodeInfo.mStatus   = status;
    nodeInfo.mOSType   = "linux";
    FillArray({CreateCPUInfo(), CreateCPUInfo(), CreateCPUInfo()}, nodeInfo.mCPUs);
    FillArray({CreatePartitionInfo("trace", {"tracefs"}), CreatePartitionInfo("tmp", {})}, nodeInfo.mPartitions);
    FillArray({CreateAttribute("attr1", "val1"), CreateAttribute("attr2", "val2")}, nodeInfo.mAttrs);
    nodeInfo.mMaxDMIPS = 429138;
    nodeInfo.mTotalRAM = 32 * 1024;

    return nodeInfo;
}

//

static iamanager::v5::CPUInfo CreateCPUInfoProto()
{
    iamanager::v5::CPUInfo cpuInfo;

    cpuInfo.set_model_name("11th Gen Intel(R) Core(TM) i7-1185G7 @ 3.00GHz");
    cpuInfo.set_num_cores(4);
    cpuInfo.set_num_threads(4);
    cpuInfo.set_arch("GenuineIntel");
    cpuInfo.set_arch_family("6");

    return cpuInfo;
}

static iamanager::v5::PartitionInfo CreatePartitionInfoProto(
    const char* name, const std::initializer_list<const char*> types)
{
    iamanager::v5::PartitionInfo partitionInfo;

    partitionInfo.set_name(name);
    partitionInfo.set_path("/sys/kernel/tracing");
    FillArray(types, *partitionInfo.mutable_types());
    partitionInfo.set_total_size(16169908);

    return partitionInfo;
}

static iamanager::v5::NodeAttribute CreateAttributeProto(const char* name, const char* value)
{
    iamanager::v5::NodeAttribute attribute;

    attribute.set_name(name);
    attribute.set_value(value);

    return attribute;
}

static iamanager::v5::NodeInfo DefaultNodeInfoProto(const std::string& status = "provisioned")
{
    iamanager::v5::NodeInfo nodeInfo;

    nodeInfo.set_node_id("node0");
    nodeInfo.set_node_type("main");
    nodeInfo.set_name("node0");
    nodeInfo.set_status(status);
    nodeInfo.set_os_type("linux");
    FillArray({CreateCPUInfoProto(), CreateCPUInfoProto(), CreateCPUInfoProto()}, *nodeInfo.mutable_cpus());
    FillArray({CreatePartitionInfoProto("trace", {"tracefs"}), CreatePartitionInfoProto("tmp", {})},
        *nodeInfo.mutable_partitions());
    FillArray(
        {CreateAttributeProto("attr1", "val1"), CreateAttributeProto("attr2", "val2")}, *nodeInfo.mutable_attrs());
    nodeInfo.set_max_dmips(429138);
    nodeInfo.set_total_ram(32 * 1024);

    return nodeInfo;
}

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class TestPublicNodeService : public iamanager::v5::IAMPublicNodesService::Service {
public:
    TestPublicNodeService(const std::string& url)
    {
        mStream                 = nullptr;
        const auto& credentials = grpc::InsecureServerCredentials();

        mServer = CreatePublicServer(url, credentials);
    }

    ~TestPublicNodeService()
    {
        if (mRegisterNodeContext) {
            mRegisterNodeContext->TryCancel();
        }
    }

    grpc::Status RegisterNode(grpc::ServerContext*                                                        context,
        grpc::ServerReaderWriter<iamanager::v5::IAMIncomingMessages, iamanager::v5::IAMOutgoingMessages>* stream)
    {
        LOG_INF() << "Test server message thread started";

        try {

            mStream              = stream;
            mRegisterNodeContext = context;

            iamanager::v5::IAMOutgoingMessages incomingMsg;

            while (stream->Read(&incomingMsg)) {
                if (incomingMsg.has_node_info()) {

                    OnNodeInfo(incomingMsg.node_info());
                    mNodeInfoCV.notify_all();
                } else if (incomingMsg.has_start_provisioning_response()) {
                    const auto errorInfo = incomingMsg.start_provisioning_response().error();

                    OnStartProvisioningResponse(errorInfo);
                    mResponseCV.notify_all();
                } else if (incomingMsg.has_finish_provisioning_response()) {
                    const auto errorInfo = incomingMsg.finish_provisioning_response().error();

                    OnFinishProvisioningResponse(errorInfo);
                    mResponseCV.notify_all();
                } else if (incomingMsg.has_deprovision_response()) {
                    const auto errorInfo = incomingMsg.deprovision_response().error();

                    OnDeprovisionResponse(errorInfo);
                    mResponseCV.notify_all();
                } else if (incomingMsg.has_pause_node_response()) {
                    const auto errorInfo = incomingMsg.pause_node_response().error();

                    OnPauseNodeResponse(errorInfo);
                    mResponseCV.notify_all();
                } else if (incomingMsg.has_resume_node_response()) {
                    const auto errorInfo = incomingMsg.resume_node_response().error();

                    OnResumeNodeResponse(errorInfo);
                    mResponseCV.notify_all();
                } else if (incomingMsg.has_create_key_response()) {
                    const auto& response = incomingMsg.create_key_response();

                    OnCreateKeyResponse(response.type(), response.csr(), response.error());
                    mResponseCV.notify_all();
                } else if (incomingMsg.has_apply_cert_response()) {
                    const auto& response = incomingMsg.apply_cert_response();

                    OnApplyCertResponse(response.type(), response.cert_url(), response.serial(), response.error());
                    mResponseCV.notify_all();
                } else if (incomingMsg.has_cert_types_response()) {
                    const auto& response = incomingMsg.cert_types_response();

                    OnCertTypesResponse(ConvertFromProtoArray(response.types()));
                    mResponseCV.notify_all();
                }
            }
        } catch (const std::exception& e) {
            LOG_ERR() << e.what();
        }

        LOG_DBG() << "Test server message thread stoped";

        mRegisterNodeContext = nullptr;

        return grpc::Status::OK;
    }

    void StartProvisioningRequest(const std::string& id, const std::string& password)
    {
        iamanager::v5::IAMIncomingMessages request;

        request.mutable_start_provisioning_request()->set_node_id(id);
        request.mutable_start_provisioning_request()->set_password(password);

        mStream->Write(request);
    }

    void FinishProvisioningRequest(const std::string& id, const std::string& password)
    {
        iamanager::v5::IAMIncomingMessages request;

        request.mutable_finish_provisioning_request()->set_node_id(id);
        request.mutable_finish_provisioning_request()->set_password(password);

        mStream->Write(request);
    }

    void DeprovisionRequest(const std::string& id, const std::string& password)
    {
        iamanager::v5::IAMIncomingMessages request;

        request.mutable_deprovision_request()->set_node_id(id);
        request.mutable_deprovision_request()->set_password(password);

        mStream->Write(request);
    }

    void PauseNodeRequest(const std::string& id)
    {
        iamanager::v5::IAMIncomingMessages request;

        request.mutable_pause_node_request()->set_node_id(id);

        mStream->Write(request);
    }

    void ResumeNodeRequest(const std::string& id)
    {
        iamanager::v5::IAMIncomingMessages request;

        request.mutable_resume_node_request()->set_node_id(id);

        mStream->Write(request);
    }

    void CreateKeyRequest(
        const std::string& id, const std::string& subject, const std::string& type, const std::string& password)
    {
        iamanager::v5::IAMIncomingMessages request;

        request.mutable_create_key_request()->set_node_id(id);
        request.mutable_create_key_request()->set_subject(subject);
        request.mutable_create_key_request()->set_type(type);
        request.mutable_create_key_request()->set_password(password);

        mStream->Write(request);
    }

    void ApplyCertRequest(const std::string& id, const std::string& type, const std::string& cert)
    {
        iamanager::v5::IAMIncomingMessages request;

        request.mutable_apply_cert_request()->set_node_id(id);
        request.mutable_apply_cert_request()->set_type(type);
        request.mutable_apply_cert_request()->set_cert(cert);

        mStream->Write(request);
    }

    void GetCertTypesRequest(const std::string& id)
    {
        iamanager::v5::IAMIncomingMessages request;

        request.mutable_get_cert_types_request()->set_node_id(id);

        mStream->Write(request);
    }

    MOCK_METHOD(void, OnNodeInfo, (const iamanager::v5::NodeInfo& nodeInfo));
    MOCK_METHOD(void, OnStartProvisioningResponse, (const ::common::v1::ErrorInfo& errorInfo));
    MOCK_METHOD(void, OnFinishProvisioningResponse, (const ::common::v1::ErrorInfo& errorInfo));
    MOCK_METHOD(void, OnDeprovisionResponse, (const ::common::v1::ErrorInfo& errorInfo));
    MOCK_METHOD(void, OnPauseNodeResponse, (const ::common::v1::ErrorInfo& errorInfo));
    MOCK_METHOD(void, OnResumeNodeResponse, (const ::common::v1::ErrorInfo& errorInfo));
    MOCK_METHOD(void, OnCreateKeyResponse,
        (const std::string& type, const std::string& csr, const ::common::v1::ErrorInfo& errorInfo));
    MOCK_METHOD(void, OnApplyCertResponse,
        (const std::string& type, const std::string& certURL, const std::string& serial,
            const ::common::v1::ErrorInfo& errorInfo));
    MOCK_METHOD(void, OnCertTypesResponse, (const std::vector<std::string>& types));

    void WaitNodeInfo(const std::chrono::seconds& timeout = std::chrono::seconds(4))
    {
        std::unique_lock lock {mLock};

        mNodeInfoCV.wait_for(lock, timeout);
    }

    void WaitResponse(const std::chrono::seconds& timeout = std::chrono::seconds(4))
    {
        std::unique_lock lock {mLock};

        mResponseCV.wait_for(lock, timeout);
    }

private:
    std::unique_ptr<grpc::Server> CreatePublicServer(
        const std::string& addr, const std::shared_ptr<grpc::ServerCredentials>& credentials)
    {
        grpc::ServerBuilder builder;

        builder.AddListeningPort(addr, credentials);
        builder.RegisterService(static_cast<iamanager::v5::IAMPublicNodesService::Service*>(this));

        return builder.BuildAndStart();
    }

    grpc::ServerReaderWriter<iamanager::v5::IAMIncomingMessages, iamanager::v5::IAMOutgoingMessages>* mStream;
    grpc::ServerContext* mRegisterNodeContext;

    std::mutex              mLock;
    std::condition_variable mNodeInfoCV;
    std::condition_variable mResponseCV;

    std::unique_ptr<grpc::Server> mServer;
};

class IAMClientTest : public Test {
protected:
    void SetUp() override { InitLog(); }

    static Config GetConfig()
    {
        Config config;

        config.mMainIAMPublicServerURL    = "localhost:5555";
        config.mMainIAMProtectedServerURL = "localhost:5556";
        config.mCertStorage               = "iam";
        config.mCACert                    = "";

        config.mStartProvisioningCmdArgs  = {"/bin/sh", "-c", "echo 'Hello World'"};
        config.mDiskEncryptionCmdArgs     = {"/bin/sh", "-c", "echo 'Hello World'"};
        config.mFinishProvisioningCmdArgs = {"/bin/sh", "-c", "echo 'Hello World'"};
        config.mDeprovisionCmdArgs        = {"/bin/sh", "-c", "echo 'Hello World'"};

        config.mNodeReconnectInterval = std::chrono::seconds(2);

        return config;
    }

    std::unique_ptr<IAMClient> CreateClient(bool provisionMode, const Config& config = GetConfig())
    {
        auto client = std::make_unique<IAMClient>();

        assert(client
                   ->Init(config, &mIdentHandler, mProvisionManager, mCertLoader, mCryptoProvider, mNodeInfoProvider,
                       provisionMode)
                   .IsNone());

        return client;
    }

    std::unique_ptr<TestPublicNodeService> CreateServer(const std::string& url)
    {
        return std::make_unique<TestPublicNodeService>(url);
    }

    std::pair<std::unique_ptr<TestPublicNodeService>, std::unique_ptr<IAMClient>> InitTest(
        const NodeStatus& status, const Config& config = GetConfig())
    {
        auto server = CreateServer(config.mMainIAMPublicServerURL);

        NodeInfo                nodeInfo    = DefaultNodeInfo(status);
        iamanager::v5::NodeInfo expNodeInfo = DefaultNodeInfoProto(status.ToString().CStr());

        EXPECT_CALL(mNodeInfoProvider, GetNodeInfo)
            .WillOnce(DoAll(SetArgReferee<0>(nodeInfo), Return(ErrorEnum::eNone)));
        EXPECT_CALL(*server, OnNodeInfo(expNodeInfo));

        auto client = CreateClient(true, config);

        server->WaitNodeInfo();

        return std::make_pair(std::move(server), std::move(client));
    }

    const String                  cSubject     = "aos-core";
    const String                  cCertType    = "iam";
    const String                  cPassword    = "admin";
    const ::common::v1::ErrorInfo cErrorInfoOK = ::common::v1::ErrorInfo();

    iam::identhandler::IdentHandlerMock         mIdentHandler;
    iam::provisionmanager::ProvisionManagerMock mProvisionManager;
    CertLoaderItfMock                           mCertLoader;
    ProviderItfMock                             mCryptoProvider;
    NodeInfoProviderMock                        mNodeInfoProvider;
};

/***********************************************************************************************************************
 * Tests
 **********************************************************************************************************************/

TEST_F(IAMClientTest, InitFailed)
{
    auto server = CreateServer(GetConfig().mMainIAMPublicServerURL);

    EXPECT_CALL(mNodeInfoProvider, GetNodeInfo).WillOnce(Return(ErrorEnum::eFailed));
    // There is no nodeInfo notification if provider failed to return it
    EXPECT_CALL(*server, OnNodeInfo(_)).Times(0);

    auto client = CreateClient(true);
    server->WaitNodeInfo(std::chrono::seconds(1));
}

TEST_F(IAMClientTest, ConnectionFailed)
{
    EXPECT_CALL(mNodeInfoProvider, GetNodeInfo).WillOnce(Return(ErrorEnum::eNone));

    auto client = CreateClient(true);
    sleep(1);
}

TEST_F(IAMClientTest, Reconnect)
{
    // Init
    auto [server1, client]              = InitTest(NodeStatusEnum::eUnprovisioned);
    NodeInfo                nodeInfo    = DefaultNodeInfo(NodeStatusEnum::eUnprovisioned);
    iamanager::v5::NodeInfo expNodeInfo = DefaultNodeInfoProto("unprovisioned");

    // close server
    server1.reset();

    // open server & wait for notification
    auto server2 = CreateServer(GetConfig().mMainIAMPublicServerURL);

    EXPECT_CALL(mNodeInfoProvider, GetNodeInfo).WillOnce(DoAll(SetArgReferee<0>(nodeInfo), Return(ErrorEnum::eNone)));
    EXPECT_CALL(*server2, OnNodeInfo(expNodeInfo));

    server2->WaitNodeInfo();
}

TEST_F(IAMClientTest, StartProvisioning)
{
    // Init
    auto [server, client] = InitTest(NodeStatusEnum::eUnprovisioned);
    NodeInfo nodeInfo     = DefaultNodeInfo(NodeStatusEnum::eUnprovisioned);

    // StartProvisioning
    EXPECT_CALL(mNodeInfoProvider, GetNodeInfo).WillOnce(DoAll(SetArgReferee<0>(nodeInfo), Return(ErrorEnum::eNone)));
    EXPECT_CALL(mProvisionManager, StartProvisioning(cPassword)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(*server, OnStartProvisioningResponse(cErrorInfoOK));

    server->StartProvisioningRequest(nodeInfo.mNodeID.CStr(), cPassword.CStr());
    server->WaitResponse();
}

TEST_F(IAMClientTest, StartProvisioningExecFailed)
{
    // Init
    auto config                      = GetConfig();
    config.mStartProvisioningCmdArgs = {"/bin/sh", "-c", "echo 'Hello World' && false"};

    auto [server, client] = InitTest(NodeStatusEnum::eUnprovisioned, config);
    NodeInfo nodeInfo     = DefaultNodeInfo(NodeStatusEnum::eUnprovisioned);

    // StartProvisioning
    EXPECT_CALL(mNodeInfoProvider, GetNodeInfo).WillOnce(DoAll(SetArgReferee<0>(nodeInfo), Return(ErrorEnum::eNone)));
    EXPECT_CALL(mProvisionManager, StartProvisioning(cPassword)).WillOnce(Return(ErrorEnum::eFailed));
    EXPECT_CALL(*server, OnStartProvisioningResponse(Not(cErrorInfoOK)));

    server->StartProvisioningRequest(nodeInfo.mNodeID.CStr(), cPassword.CStr());
    server->WaitResponse();
}

TEST_F(IAMClientTest, StartProvisioningWrongNodeStatus)
{
    // Init
    auto [server, client] = InitTest(NodeStatusEnum::eProvisioned);
    NodeInfo nodeInfo     = DefaultNodeInfo(NodeStatusEnum::eProvisioned);

    // StartProvisioning
    EXPECT_CALL(mNodeInfoProvider, GetNodeInfo).WillOnce(DoAll(SetArgReferee<0>(nodeInfo), Return(ErrorEnum::eNone)));

    EXPECT_CALL(*server, OnStartProvisioningResponse(Not(cErrorInfoOK)));

    server->StartProvisioningRequest(nodeInfo.mNodeID.CStr(), cPassword.CStr());
    server->WaitResponse();
}

TEST_F(IAMClientTest, FinishProvisioning)
{
    // Init
    auto [server, client] = InitTest(NodeStatusEnum::eUnprovisioned);
    NodeInfo nodeInfo     = DefaultNodeInfo(NodeStatusEnum::eUnprovisioned);

    // FinishProvisioning
    NodeInfo                provNodeInfo    = DefaultNodeInfo(NodeStatusEnum::eProvisioned);
    iamanager::v5::NodeInfo expProvNodeInfo = DefaultNodeInfoProto("provisioned");

    EXPECT_CALL(mNodeInfoProvider, SetNodeStatus(NodeStatus(NodeStatusEnum::eProvisioned)));
    EXPECT_CALL(mNodeInfoProvider, GetNodeInfo)
        .WillOnce(DoAll(SetArgReferee<0>(nodeInfo), Return(ErrorEnum::eNone)))
        .WillOnce(DoAll(SetArgReferee<0>(provNodeInfo), Return(ErrorEnum::eNone)));

    EXPECT_CALL(mProvisionManager, FinishProvisioning(cPassword)).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_CALL(*server, OnNodeInfo(expProvNodeInfo));
    EXPECT_CALL(*server, OnFinishProvisioningResponse(cErrorInfoOK));

    server->FinishProvisioningRequest(nodeInfo.mNodeID.CStr(), cPassword.CStr());
    server->WaitResponse();
    server->WaitNodeInfo();
}

TEST_F(IAMClientTest, FinishProvisioningWrongNodeStatus)
{
    // Init
    auto [server, client] = InitTest(NodeStatusEnum::eProvisioned);
    NodeInfo nodeInfo     = DefaultNodeInfo(NodeStatusEnum::eProvisioned);

    // FinishProvisioning
    EXPECT_CALL(mNodeInfoProvider, GetNodeInfo).WillOnce(DoAll(SetArgReferee<0>(nodeInfo), Return(ErrorEnum::eNone)));

    EXPECT_CALL(*server, OnFinishProvisioningResponse(Not(cErrorInfoOK)));

    server->FinishProvisioningRequest(nodeInfo.mNodeID.CStr(), cPassword.CStr());
    server->WaitResponse();
}

TEST_F(IAMClientTest, Deprovision)
{
    // Init
    auto [server, client] = InitTest(NodeStatusEnum::eProvisioned);
    NodeInfo nodeInfo     = DefaultNodeInfo(NodeStatusEnum::eProvisioned);

    // Deprovision
    NodeInfo                deprovNodeInfo    = DefaultNodeInfo(NodeStatusEnum::eUnprovisioned);
    iamanager::v5::NodeInfo expDeprovNodeInfo = DefaultNodeInfoProto("unprovisioned");

    EXPECT_CALL(mNodeInfoProvider, SetNodeStatus(NodeStatus(NodeStatusEnum::eUnprovisioned)));
    EXPECT_CALL(mNodeInfoProvider, GetNodeInfo)
        .WillOnce(DoAll(SetArgReferee<0>(nodeInfo), Return(ErrorEnum::eNone)))
        .WillOnce(DoAll(SetArgReferee<0>(deprovNodeInfo), Return(ErrorEnum::eNone)));

    EXPECT_CALL(mProvisionManager, Deprovision(cPassword)).WillOnce(Return(ErrorEnum::eNone));

    EXPECT_CALL(*server, OnNodeInfo(expDeprovNodeInfo));
    EXPECT_CALL(*server, OnDeprovisionResponse(::common::v1::ErrorInfo()));

    server->DeprovisionRequest(nodeInfo.mNodeID.CStr(), cPassword.CStr());
    server->WaitResponse();
    server->WaitNodeInfo();
}

TEST_F(IAMClientTest, DeprovisionWrongNodeStatus)
{
    // Init
    auto [server, client] = InitTest(NodeStatusEnum::eUnprovisioned);
    NodeInfo nodeInfo     = DefaultNodeInfo(NodeStatusEnum::eUnprovisioned);

    // Deprovision
    EXPECT_CALL(mNodeInfoProvider, GetNodeInfo).WillOnce(DoAll(SetArgReferee<0>(nodeInfo), Return(ErrorEnum::eNone)));

    EXPECT_CALL(*server, OnDeprovisionResponse(Not(cErrorInfoOK)));

    server->DeprovisionRequest(nodeInfo.mNodeID.CStr(), cPassword.CStr());
    server->WaitResponse();
}

TEST_F(IAMClientTest, PauseNode)
{
    // Init
    auto [server, client] = InitTest(NodeStatusEnum::eProvisioned);
    NodeInfo nodeInfo     = DefaultNodeInfo(NodeStatusEnum::eProvisioned);

    // Pause
    NodeInfo                pausedNodeInfo    = DefaultNodeInfo(NodeStatusEnum::ePaused);
    iamanager::v5::NodeInfo expPausedNodeInfo = DefaultNodeInfoProto("paused");

    EXPECT_CALL(mNodeInfoProvider, SetNodeStatus(NodeStatus(NodeStatusEnum::ePaused)));
    EXPECT_CALL(mNodeInfoProvider, GetNodeInfo)
        .WillOnce(DoAll(SetArgReferee<0>(nodeInfo), Return(ErrorEnum::eNone)))
        .WillOnce(DoAll(SetArgReferee<0>(pausedNodeInfo), Return(ErrorEnum::eNone)));

    EXPECT_CALL(*server, OnNodeInfo(expPausedNodeInfo));
    EXPECT_CALL(*server, OnPauseNodeResponse(::common::v1::ErrorInfo()));

    server->PauseNodeRequest(nodeInfo.mNodeID.CStr());
    server->WaitResponse();
    server->WaitNodeInfo();
}

TEST_F(IAMClientTest, PauseWrongNodeStatus)
{
    // Init
    auto [server, client] = InitTest(NodeStatusEnum::eUnprovisioned);
    NodeInfo nodeInfo     = DefaultNodeInfo(NodeStatusEnum::eUnprovisioned);

    // Pause
    EXPECT_CALL(mNodeInfoProvider, GetNodeInfo).WillOnce(DoAll(SetArgReferee<0>(nodeInfo), Return(ErrorEnum::eNone)));

    EXPECT_CALL(*server, OnPauseNodeResponse(Not(cErrorInfoOK)));

    server->PauseNodeRequest(nodeInfo.mNodeID.CStr());
    server->WaitResponse();
}

TEST_F(IAMClientTest, ResumeNode)
{
    // Init
    auto [server, client] = InitTest(NodeStatusEnum::ePaused);
    NodeInfo nodeInfo     = DefaultNodeInfo(NodeStatusEnum::ePaused);

    // Resume
    NodeInfo                resumedNodeInfo    = DefaultNodeInfo(NodeStatusEnum::eProvisioned);
    iamanager::v5::NodeInfo expResumedNodeInfo = DefaultNodeInfoProto("provisioned");

    EXPECT_CALL(mNodeInfoProvider, SetNodeStatus(NodeStatus(NodeStatusEnum::eProvisioned)));
    EXPECT_CALL(mNodeInfoProvider, GetNodeInfo)
        .WillOnce(DoAll(SetArgReferee<0>(nodeInfo), Return(ErrorEnum::eNone)))
        .WillOnce(DoAll(SetArgReferee<0>(resumedNodeInfo), Return(ErrorEnum::eNone)));

    EXPECT_CALL(*server, OnNodeInfo(expResumedNodeInfo));
    EXPECT_CALL(*server, OnResumeNodeResponse(::common::v1::ErrorInfo()));

    server->ResumeNodeRequest(nodeInfo.mNodeID.CStr());
    server->WaitResponse();
    server->WaitNodeInfo();
}

TEST_F(IAMClientTest, ResumeWrongNodeStatus)
{
    // Init
    auto [server, client] = InitTest(NodeStatusEnum::eUnprovisioned);
    NodeInfo nodeInfo     = DefaultNodeInfo(NodeStatusEnum::eUnprovisioned);

    // Resume
    EXPECT_CALL(mNodeInfoProvider, GetNodeInfo).WillOnce(DoAll(SetArgReferee<0>(nodeInfo), Return(ErrorEnum::eNone)));

    EXPECT_CALL(*server, OnResumeNodeResponse(Not(cErrorInfoOK)));

    server->ResumeNodeRequest(nodeInfo.mNodeID.CStr());
    server->WaitResponse();
}

TEST_F(IAMClientTest, CreateKey)
{
    // Init
    auto [server, client] = InitTest(NodeStatusEnum::eUnprovisioned);
    NodeInfo nodeInfo     = DefaultNodeInfo(NodeStatusEnum::eUnprovisioned);

    // CreateKey
    EXPECT_CALL(mProvisionManager, CreateKey(cCertType, cSubject, cPassword, _)).WillOnce(Return(ErrorEnum::eNone));
    EXPECT_CALL(*server, OnCreateKeyResponse(std::string(cCertType.CStr()), _, ::common::v1::ErrorInfo()));
    EXPECT_CALL(mIdentHandler, GetSystemID())
        .WillOnce(Return(RetWithError<StaticString<cSystemIDLen>>(cSubject, ErrorEnum::eNone)));

    server->CreateKeyRequest(nodeInfo.mNodeID.CStr(), "", cCertType.CStr(), cPassword.CStr());
    server->WaitResponse();
}

TEST_F(IAMClientTest, ApplyCert)
{
    // Init
    auto [server, client] = InitTest(NodeStatusEnum::eUnprovisioned);
    NodeInfo nodeInfo     = DefaultNodeInfo(NodeStatusEnum::eUnprovisioned);

    // ApplyCert
    iam::certhandler::CertInfo certInfo;

    EXPECT_CALL(mProvisionManager, ApplyCert(cCertType, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(certInfo), Return(ErrorEnum::eNone)));
    EXPECT_CALL(*server,
        OnApplyCertResponse(
            std::string(cCertType.CStr()), std::string(certInfo.mCertURL.CStr()), _, ::common::v1::ErrorInfo()));

    server->ApplyCertRequest(nodeInfo.mNodeID.CStr(), cCertType.CStr(), {});
    server->WaitResponse();
}

TEST_F(IAMClientTest, GetCertTypes)
{
    // Init
    auto [server, client] = InitTest(NodeStatusEnum::eUnprovisioned);
    NodeInfo nodeInfo     = DefaultNodeInfo(NodeStatusEnum::eUnprovisioned);

    // GetCertTypes
    aos::iam::provisionmanager::CertTypes types;
    FillArray({"iam", "online", "offline"}, types);

    EXPECT_CALL(mProvisionManager, GetCertTypes())
        .WillOnce(Return(aos::RetWithError<aos::iam::provisionmanager::CertTypes>(types)));
    EXPECT_CALL(*server, OnCertTypesResponse(ElementsAre("iam", "online", "offline")));

    server->GetCertTypesRequest(nodeInfo.mNodeID.CStr());
    server->WaitResponse();
}
