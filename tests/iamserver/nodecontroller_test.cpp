/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <thread>

#include <gmock/gmock.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/server_builder.h>
#include <openssl/engine.h>

#include <test/utils/log.hpp>

#include <iamanager/v5/iamanager.grpc.pb.h>

#include "iamserver/nodecontroller.hpp"
#include "mocks/nodemanagermock.hpp"

using namespace aos;
using namespace testing;

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

static constexpr auto cServerURL         = "0.0.0.0:50051";
static const auto     cProvisionedStatus = aos::NodeStatus(aos::NodeStatusEnum::eProvisioned);

namespace {
class TestServer : public iamproto::IAMPublicNodesService::Service {
public:
    MOCK_METHOD(grpc::Status, GetAllNodeIDs, (grpc::ServerContext*, const google::protobuf::Empty*, iamproto::NodesID*),
        (override));

    MOCK_METHOD(grpc::Status, GetNodeInfo,
        (grpc::ServerContext*, const iamproto::GetNodeInfoRequest*, iamproto::NodeInfo*), (override));

    MOCK_METHOD(grpc::Status, SubscribeNodeChanged,
        (grpc::ServerContext*, const google::protobuf::Empty*, grpc::ServerWriter<iamproto::NodeInfo>*), (override));

    grpc::Status RegisterNode(grpc::ServerContext*                                                  context,
        grpc::ServerReaderWriter<::iamproto::IAMIncomingMessages, ::iamproto::IAMOutgoingMessages>* stream) override
    {

        return mNodeController.HandleRegisterNodeStream(
            {aos::NodeStatusEnum::eProvisioned}, stream, context, &mNodeManager);
    }

    void Start()
    {
        grpc::ServerBuilder serverBuilder;

        serverBuilder.AddListeningPort(cServerURL, grpc::InsecureServerCredentials());
        serverBuilder.RegisterService(this);

        mServer = serverBuilder.BuildAndStart();

        if (!mServer) {
            LOG_ERR() << "Failed to start public TestServer: URL = " << cServerURL;

            return;
        }
    }

    void Stop()
    {
        mNodeController.Close();

        if (mServer) {
            mServer->Shutdown();
            mServer->Wait();
        }
    }

    NodeController* GetNodeController() { return &mNodeController; }

private:
    std::unique_ptr<grpc::Server> mServer;
    NodeController                mNodeController;
    NodeManagerMock               mNodeManager;
};

std::unique_ptr<iamproto::IAMPublicNodesService::Stub> CreateClientStub(const std::string& url)
{
    auto channel = grpc::CreateCustomChannel(url, grpc::InsecureChannelCredentials(), grpc::ChannelArguments());
    if (channel == nullptr) {
        return nullptr;
    }

    return iamproto::IAMPublicNodesService::NewStub(channel);
}

} // namespace

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class NodeControllerTest : public Test {
protected:
    NodeController* GetNodeController() { return mServer.GetNodeController(); }
    auto            CreateRegisterNodeClientStream() { return mClientStub->RegisterNode(&mContext); }

private:
    void SetUp() override
    {
        aos::InitLog();

        mServer.Start();

        mClientStub = CreateClientStub(cServerURL);
        ASSERT_NE(mClientStub, nullptr);
    }

    void TearDown() override
    {
        mClientStub.reset();

        mServer.Stop();
    }

protected:
    TestServer                                             mServer;
    std::unique_ptr<iamproto::IAMPublicNodesService::Stub> mClientStub;
    grpc::ClientContext                                    mContext;
    iamproto::IAMOutgoingMessages                          mOutgoingMessage;
    iamproto::IAMIncomingMessages                          mIncomingMessage;
};

TEST_F(NodeControllerTest, RegisterNode)
{
    auto stream = CreateRegisterNodeClientStream();
    ASSERT_NE(stream, nullptr) << "Failed to create client stream";

    mOutgoingMessage.mutable_node_info()->set_node_id("node1");
    mOutgoingMessage.mutable_node_info()->set_status(cProvisionedStatus.ToString().CStr());

    stream->Write(mOutgoingMessage);

    stream->WritesDone();
}

TEST_F(NodeControllerTest, RegisterNode2ClientsWithSameID)
{
    std::vector<std::unique_ptr<iamproto::IAMPublicNodesService::Stub>> stubs;
    std::vector<std::unique_ptr<grpc::ClientReaderWriter<iamproto::IAMOutgoingMessages, iamproto::IAMIncomingMessages>>>
        streams;

    mOutgoingMessage.mutable_node_info()->set_node_id("node1");
    mOutgoingMessage.mutable_node_info()->set_status(cProvisionedStatus.ToString().CStr());

    for (size_t i = 0; i < 2; ++i) {
        stubs.push_back(CreateClientStub(cServerURL));

        auto& stub = stubs.back();

        ASSERT_NE(stub, nullptr);
    }

    for (auto& stub : stubs) {
        grpc::ClientContext context;

        auto stream = stub->RegisterNode(&context);
        ASSERT_NE(stream, nullptr) << "Failed to create client stream";

        stream->Write(mOutgoingMessage);
    }

    for (auto& stream : streams) {
        auto status = stream->Finish();

        ASSERT_TRUE(status.ok()) << "Failed to finish stream";

        stream->WritesDone();
    }
}

TEST_F(NodeControllerTest, StartProvisioningFailsOnUnknownNodeID)
{
    auto stream = CreateRegisterNodeClientStream();
    ASSERT_NE(stream, nullptr) << "Failed to create client stream";

    mOutgoingMessage.mutable_node_info()->set_node_id("node1");
    mOutgoingMessage.mutable_node_info()->set_status(cProvisionedStatus.ToString().CStr());

    stream->Write(mOutgoingMessage);

    auto steamHandler = GetNodeController()->GetNodeStreamHandler("unknown");
    EXPECT_EQ(steamHandler, nullptr);

    stream->WritesDone();
}

TEST_F(NodeControllerTest, StartProvisioningFailsDueTimeout)
{
    auto stream = CreateRegisterNodeClientStream();
    ASSERT_NE(stream, nullptr) << "Failed to create client stream";

    mOutgoingMessage.mutable_node_info()->set_node_id("node1");
    mOutgoingMessage.mutable_node_info()->set_status(cProvisionedStatus.ToString().CStr());

    stream->Write(mOutgoingMessage);

    iamproto::StartProvisioningRequest  request;
    iamproto::StartProvisioningResponse response;

    request.set_node_id("node1");

    grpc::Status status;

    for (size_t i = 1; i < 4; ++i) {
        auto steamHandler = GetNodeController()->GetNodeStreamHandler("node1");
        if (!steamHandler) {
            LOG_ERR() << "Node stream handler not found: nodeID = node1";

            std::this_thread::sleep_for(std::chrono::milliseconds(100 * i));

            continue;
        }

        status = steamHandler->StartProvisioning(&request, &response, std::chrono::seconds(1));

        break;
    }

    ASSERT_TRUE(!status.ok());

    stream->WritesDone();
}

TEST_F(NodeControllerTest, StartProvisioningSucceeds)
{
    auto stream = CreateRegisterNodeClientStream();
    ASSERT_NE(stream, nullptr) << "Failed to create client stream";

    mOutgoingMessage.mutable_node_info()->set_node_id("node1");
    mOutgoingMessage.mutable_node_info()->set_status(cProvisionedStatus.ToString().CStr());

    ASSERT_TRUE(stream->Write(mOutgoingMessage));

    iamproto::StartProvisioningRequest  request;
    iamproto::StartProvisioningResponse response;

    request.set_node_id("node1");

    auto async = std::async(std::launch::async, [&]() {
        ASSERT_TRUE(stream->Read(&mIncomingMessage));
        ASSERT_TRUE(mIncomingMessage.has_start_provisioning_request());
        ASSERT_EQ(mIncomingMessage.start_provisioning_request().node_id(), "node1");

        iamproto::IAMOutgoingMessages mOutgoingMessage;
        mOutgoingMessage.mutable_start_provisioning_response();

        ASSERT_TRUE(stream->Write(mOutgoingMessage));
    });

    grpc::Status status;

    for (size_t i = 1; i < 4; ++i) {
        auto steamHandler = GetNodeController()->GetNodeStreamHandler("node1");
        if (!steamHandler) {
            LOG_ERR() << "Node stream handler not found: nodeID = node1";

            std::this_thread::sleep_for(std::chrono::milliseconds(100 * i));

            continue;
        }

        status = steamHandler->StartProvisioning(&request, &response, std::chrono::seconds(1));

        if (status.ok()) {
            break;
        }
    }

    stream->WritesDone();

    ASSERT_TRUE(status.ok()) << status.error_message();
}

TEST_F(NodeControllerTest, FinishProvisioningSucceeds)
{
    auto stream = CreateRegisterNodeClientStream();
    ASSERT_NE(stream, nullptr) << "Failed to create client stream";

    mOutgoingMessage.mutable_node_info()->set_node_id("node1");
    mOutgoingMessage.mutable_node_info()->set_status(cProvisionedStatus.ToString().CStr());

    stream->Write(mOutgoingMessage);

    iamproto::FinishProvisioningRequest  request;
    iamproto::FinishProvisioningResponse response;

    request.set_node_id("node1");

    auto async = std::async(std::launch::async, [&]() {
        ASSERT_TRUE(stream->Read(&mIncomingMessage));
        ASSERT_TRUE(mIncomingMessage.has_finish_provisioning_request());
        ASSERT_EQ(mIncomingMessage.finish_provisioning_request().node_id(), "node1");

        LOG_DBG() << "Received finish provisioning request: " << mIncomingMessage.DebugString().c_str();

        iamproto::IAMOutgoingMessages mOutgoingMessage;
        mOutgoingMessage.mutable_finish_provisioning_response();

        ASSERT_TRUE(stream->Write(mOutgoingMessage));
    });

    grpc::Status status;

    for (size_t i = 1; i < 4; ++i) {
        auto steamHandler = GetNodeController()->GetNodeStreamHandler("node1");
        if (!steamHandler) {
            LOG_ERR() << "Node stream handler not found: nodeID = node1";

            std::this_thread::sleep_for(std::chrono::milliseconds(100 * i));

            continue;
        }

        status = steamHandler->FinishProvisioning(&request, &response, std::chrono::seconds(1));

        if (status.ok()) {
            break;
        }
    }

    stream->WritesDone();

    ASSERT_TRUE(status.ok()) << status.error_message();
}

TEST_F(NodeControllerTest, DeprovisionSucceeds)
{
    auto stream = CreateRegisterNodeClientStream();
    ASSERT_NE(stream, nullptr) << "Failed to create client stream";

    mOutgoingMessage.mutable_node_info()->set_node_id("node1");
    mOutgoingMessage.mutable_node_info()->set_status(cProvisionedStatus.ToString().CStr());

    stream->Write(mOutgoingMessage);

    iamproto::DeprovisionRequest  request;
    iamproto::DeprovisionResponse response;

    request.set_node_id("node1");

    auto async = std::async(std::launch::async, [&]() {
        ASSERT_TRUE(stream->Read(&mIncomingMessage));
        ASSERT_TRUE(mIncomingMessage.has_deprovision_request());
        ASSERT_EQ(mIncomingMessage.deprovision_request().node_id(), "node1");

        LOG_DBG() << "Received finish provisioning request: " << mIncomingMessage.DebugString().c_str();

        iamproto::IAMOutgoingMessages mOutgoingMessage;
        mOutgoingMessage.mutable_deprovision_response();

        ASSERT_TRUE(stream->Write(mOutgoingMessage));
    });

    grpc::Status status;

    for (size_t i = 1; i < 4; ++i) {
        auto steamHandler = GetNodeController()->GetNodeStreamHandler("node1");
        if (!steamHandler) {
            LOG_ERR() << "Node stream handler not found: nodeID = node1";

            std::this_thread::sleep_for(std::chrono::milliseconds(100 * i));

            continue;
        }

        status = steamHandler->Deprovision(&request, &response, std::chrono::seconds(1));

        if (status.ok()) {
            break;
        }
    }

    stream->WritesDone();

    ASSERT_TRUE(status.ok()) << status.error_message();
}

TEST_F(NodeControllerTest, PauseNodeSucceeds)
{
    auto stream = CreateRegisterNodeClientStream();
    ASSERT_NE(stream, nullptr) << "Failed to create client stream";

    mOutgoingMessage.mutable_node_info()->set_node_id("node1");
    mOutgoingMessage.mutable_node_info()->set_status(cProvisionedStatus.ToString().CStr());

    stream->Write(mOutgoingMessage);

    iamproto::PauseNodeRequest  request;
    iamproto::PauseNodeResponse response;

    request.set_node_id("node1");

    auto async = std::async(std::launch::async, [&]() {
        ASSERT_TRUE(stream->Read(&mIncomingMessage));
        ASSERT_TRUE(mIncomingMessage.has_pause_node_request());
        ASSERT_EQ(mIncomingMessage.pause_node_request().node_id(), "node1");

        LOG_DBG() << "Received finish provisioning request: " << mIncomingMessage.DebugString().c_str();

        iamproto::IAMOutgoingMessages mOutgoingMessage;
        mOutgoingMessage.mutable_pause_node_response();

        ASSERT_TRUE(stream->Write(mOutgoingMessage));
    });

    grpc::Status status;

    for (size_t i = 1; i < 4; ++i) {
        auto steamHandler = GetNodeController()->GetNodeStreamHandler("node1");
        if (!steamHandler) {
            LOG_ERR() << "Node stream handler not found: nodeID = node1";

            std::this_thread::sleep_for(std::chrono::milliseconds(100 * i));

            continue;
        }

        status = steamHandler->PauseNode(&request, &response, std::chrono::seconds(1));

        if (status.ok()) {
            break;
        }
    }

    stream->WritesDone();

    ASSERT_TRUE(status.ok()) << status.error_message();
}

TEST_F(NodeControllerTest, ResumeNodeSucceeds)
{
    auto stream = CreateRegisterNodeClientStream();
    ASSERT_NE(stream, nullptr) << "Failed to create client stream";

    mOutgoingMessage.mutable_node_info()->set_node_id("node1");
    mOutgoingMessage.mutable_node_info()->set_status(cProvisionedStatus.ToString().CStr());

    stream->Write(mOutgoingMessage);

    iamproto::ResumeNodeRequest  request;
    iamproto::ResumeNodeResponse response;

    request.set_node_id("node1");

    auto async = std::async(std::launch::async, [&]() {
        ASSERT_TRUE(stream->Read(&mIncomingMessage));
        ASSERT_TRUE(mIncomingMessage.has_resume_node_request());
        ASSERT_EQ(mIncomingMessage.resume_node_request().node_id(), "node1");

        LOG_DBG() << "Received finish provisioning request: " << mIncomingMessage.DebugString().c_str();

        iamproto::IAMOutgoingMessages mOutgoingMessage;
        mOutgoingMessage.mutable_resume_node_response();

        ASSERT_TRUE(stream->Write(mOutgoingMessage));
    });

    grpc::Status status;

    for (size_t i = 1; i < 4; ++i) {
        auto steamHandler = GetNodeController()->GetNodeStreamHandler("node1");
        if (!steamHandler) {
            LOG_ERR() << "Node stream handler not found: nodeID = node1";

            std::this_thread::sleep_for(std::chrono::milliseconds(100 * i));

            continue;
        }

        status = steamHandler->ResumeNode(&request, &response, std::chrono::seconds(1));

        if (status.ok()) {
            break;
        }
    }

    stream->WritesDone();

    ASSERT_TRUE(status.ok()) << status.error_message();
}

TEST_F(NodeControllerTest, CreateKeySucceeds)
{
    auto stream = CreateRegisterNodeClientStream();
    ASSERT_NE(stream, nullptr) << "Failed to create client stream";

    mOutgoingMessage.mutable_node_info()->set_node_id("node1");
    mOutgoingMessage.mutable_node_info()->set_status(cProvisionedStatus.ToString().CStr());

    stream->Write(mOutgoingMessage);

    iamproto::CreateKeyRequest  request;
    iamproto::CreateKeyResponse response;

    request.set_node_id("node1");

    auto async = std::async(std::launch::async, [&]() {
        ASSERT_TRUE(stream->Read(&mIncomingMessage));
        ASSERT_TRUE(mIncomingMessage.has_create_key_request());
        ASSERT_EQ(mIncomingMessage.create_key_request().node_id(), "node1");

        LOG_DBG() << "Received finish provisioning request: " << mIncomingMessage.DebugString().c_str();

        iamproto::IAMOutgoingMessages mOutgoingMessage;
        mOutgoingMessage.mutable_create_key_response();

        ASSERT_TRUE(stream->Write(mOutgoingMessage));
    });

    grpc::Status status;

    for (size_t i = 1; i < 4; ++i) {
        auto steamHandler = GetNodeController()->GetNodeStreamHandler("node1");
        if (!steamHandler) {
            LOG_ERR() << "Node stream handler not found: nodeID = node1";

            std::this_thread::sleep_for(std::chrono::milliseconds(100 * i));

            continue;
        }

        status = steamHandler->CreateKey(&request, &response, std::chrono::seconds(1));

        if (status.ok()) {
            break;
        }
    }

    stream->WritesDone();

    ASSERT_TRUE(status.ok()) << status.error_message();
}

TEST_F(NodeControllerTest, ApplyCertSucceeds)
{
    auto stream = CreateRegisterNodeClientStream();
    ASSERT_NE(stream, nullptr) << "Failed to create client stream";

    mOutgoingMessage.mutable_node_info()->set_node_id("node1");
    mOutgoingMessage.mutable_node_info()->set_status(cProvisionedStatus.ToString().CStr());

    stream->Write(mOutgoingMessage);

    iamproto::ApplyCertRequest  request;
    iamproto::ApplyCertResponse response;

    request.set_node_id("node1");

    auto async = std::async(std::launch::async, [&]() {
        ASSERT_TRUE(stream->Read(&mIncomingMessage));
        ASSERT_TRUE(mIncomingMessage.has_apply_cert_request());
        ASSERT_EQ(mIncomingMessage.apply_cert_request().node_id(), "node1");

        LOG_DBG() << "Received finish provisioning request: " << mIncomingMessage.DebugString().c_str();

        iamproto::IAMOutgoingMessages mOutgoingMessage;
        mOutgoingMessage.mutable_apply_cert_response();

        ASSERT_TRUE(stream->Write(mOutgoingMessage));
    });

    grpc::Status status;

    for (size_t i = 1; i < 4; ++i) {
        auto steamHandler = GetNodeController()->GetNodeStreamHandler("node1");
        if (!steamHandler) {
            LOG_ERR() << "Node stream handler not found: nodeID = node1";

            std::this_thread::sleep_for(std::chrono::milliseconds(100 * i));

            continue;
        }

        status = steamHandler->ApplyCert(&request, &response, std::chrono::seconds(1));

        if (status.ok()) {
            break;
        }
    }

    stream->WritesDone();

    ASSERT_TRUE(status.ok()) << status.error_message();
}
