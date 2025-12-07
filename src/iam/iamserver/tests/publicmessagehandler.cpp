/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <thread>
#include <vector>

#include <gmock/gmock.h>

#include <core/common/crypto/cryptoprovider.hpp>
#include <core/common/tests/mocks/certprovidermock.hpp>
#include <core/common/tests/mocks/identprovidermock.hpp>
#include <core/common/tests/utils/log.hpp>
#include <core/iam/certhandler/certhandler.hpp>
#include <core/iam/certhandler/certmodules/pkcs11/pkcs11.hpp>
#include <core/iam/tests/mocks/currentnodemock.hpp>
#include <core/iam/tests/mocks/nodemanagermock.hpp>
#include <core/iam/tests/mocks/permhandlermock.hpp>
#include <core/iam/tests/mocks/provisionmanagermock.hpp>

#include <common/utils/grpchelper.hpp>
#include <iam/iamserver/publicmessagehandler.hpp>

#include "stubs/storagestub.hpp"

using namespace testing;

namespace aos::iam::iamserver {

namespace {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

constexpr auto cServerURL = "0.0.0.0:4456";
constexpr auto cSystemID  = "system-id";
constexpr auto cUnitModel = "unit-model";

template <typename T>
std::unique_ptr<typename T::Stub> CreateClientStub()
{
    auto tlsChannelCreds = grpc::InsecureChannelCredentials();

    if (tlsChannelCreds == nullptr) {
        return nullptr;
    }

    auto channel = grpc::CreateCustomChannel(cServerURL, tlsChannelCreds, grpc::ChannelArguments());
    if (channel == nullptr) {
        return nullptr;
    }

    return T::NewStub(channel);
}

} // namespace

/***********************************************************************************************************************
 * Suite
 **********************************************************************************************************************/

class PublicMessageHandlerTest : public Test {
protected:
    NodeController                mNodeController;
    PublicMessageHandler          mPublicMessageHandler;
    std::unique_ptr<grpc::Server> mPublicServer;

    // mocks
    iamclient::IdentProviderMock           mIdentProvider;
    permhandler::PermHandlerMock           mPermHandler;
    currentnode::CurrentNodeHandlerMock    mCurrentNodeHandler;
    nodemanager::NodeManagerMock           mNodeManager;
    iamclient::CertProviderMock            mCertProvider;
    provisionmanager::ProvisionManagerMock mProvisionManager;

private:
    void SetUp() override;
    void TearDown() override;
};

void PublicMessageHandlerTest::SetUp()
{
    tests::utils::InitLog();

    EXPECT_CALL(mCurrentNodeHandler, GetCurrentNodeInfo).WillRepeatedly(Invoke([&](NodeInfo& nodeInfo) {
        nodeInfo.mNodeID   = "node0";
        nodeInfo.mNodeType = "test-type";
        nodeInfo.mAttrs.PushBack({"MainNode", ""});

        LOG_DBG() << "CurrentNodeHandler::GetCurrentNodeInfo: " << nodeInfo.mNodeID.CStr() << ", "
                  << nodeInfo.mNodeType.CStr();

        return ErrorEnum::eNone;
    }));

    auto err = mPublicMessageHandler.Init(
        mNodeController, mIdentProvider, mPermHandler, mCurrentNodeHandler, mNodeManager, mCertProvider);

    ASSERT_TRUE(err.IsNone()) << "Failed to initialize public message handler: " << err.Message();

    grpc::ServerBuilder builder;
    builder.AddListeningPort(cServerURL, grpc::InsecureServerCredentials());
    mPublicMessageHandler.RegisterServices(builder);
    mPublicServer = builder.BuildAndStart();
}

void PublicMessageHandlerTest::TearDown()
{
    if (mPublicServer) {
        mPublicServer->Shutdown();
        mPublicServer->Wait();
    }

    mPublicMessageHandler.Close();
}

/***********************************************************************************************************************
 * IAMVersionService tests
 **********************************************************************************************************************/

TEST_F(PublicMessageHandlerTest, GetAPIVersionSucceeds)
{
    auto clientStub = CreateClientStub<iamanager::IAMVersionService>();
    ASSERT_NE(clientStub, nullptr) << "Failed to create client stub";

    grpc::ClientContext     context;
    google::protobuf::Empty request;
    iamanager::APIVersion   response;

    const auto status = clientStub->GetAPIVersion(&context, request, &response);

    ASSERT_TRUE(status.ok()) << "GetAPIVersion failed: code = " << status.error_code()
                             << ", message = " << status.error_message();

    ASSERT_EQ(response.version(), 6);
}

/***********************************************************************************************************************
 * IAMPublicService tests
 **********************************************************************************************************************/

TEST_F(PublicMessageHandlerTest, GetCurrentNodeInfo)
{
    auto clientStub = CreateClientStub<iamproto::IAMPublicCurrentNodeService>();
    ASSERT_NE(clientStub, nullptr) << "Failed to create client stub";

    grpc::ClientContext     context;
    google::protobuf::Empty request;
    iamproto::NodeInfo      response;

    const auto status = clientStub->GetCurrentNodeInfo(&context, request, &response);

    ASSERT_TRUE(status.ok()) << "GetNodeInfo failed: code = " << status.error_code()
                             << ", message = " << status.error_message();

    ASSERT_EQ(response.node_id(), "node0");
    ASSERT_EQ(response.node_type(), "test-type");
}

TEST_F(PublicMessageHandlerTest, SubscribeCurrentNodeChanged)
{
    auto clientStub = CreateClientStub<iamproto::IAMPublicCurrentNodeService>();
    ASSERT_NE(clientStub, nullptr) << "Failed to create client stub";

    grpc::ClientContext     context;
    google::protobuf::Empty request;
    iamproto::NodeInfo      response;

    auto reader = clientStub->SubscribeCurrentNodeChanged(&context, request);

    auto nodeInfoChanged       = std::make_unique<NodeInfo>();
    nodeInfoChanged->mNodeID   = "unknown";
    nodeInfoChanged->mNodeType = "test-type-updated";

    // Node info change notification for another node - should be ignored.
    mPublicMessageHandler.OnNodeInfoChange(*nodeInfoChanged);

    nodeInfoChanged->mNodeID = "node0";

    // Node info change notification for this node - should be processed.
    mPublicMessageHandler.OnNodeInfoChange(*nodeInfoChanged);

    ASSERT_NE(reader.get(), nullptr) << "Failed to create stream reader";

    while (reader->Read(&response)) {
        ASSERT_EQ(response.node_id(), "node0");
        ASSERT_EQ(response.node_type(), "test-type-updated");

        break;
    }

    context.TryCancel();

    auto status = reader->Finish();

    ASSERT_EQ(status.error_code(), grpc::StatusCode::CANCELLED)
        << "Stream finish should return CANCELLED code: code = " << status.error_code()
        << ", message = " << status.error_message();
}

/***********************************************************************************************************************
 * IAMPublicCurrentNodeService tests
 **********************************************************************************************************************/

TEST_F(PublicMessageHandlerTest, GetCertSucceeds)
{
    auto clientStub = CreateClientStub<iamproto::IAMPublicCertService>();
    ASSERT_NE(clientStub, nullptr) << "Failed to create client stub";

    grpc::ClientContext      context;
    iamproto::GetCertRequest request;
    iamproto::CertInfo       response;

    request.set_issuer("test-issuer");
    request.set_serial("58bdb46d06865f7f");
    request.set_type("test-type");

    CertInfo certInfo;
    certInfo.mKeyURL  = "test-key-url";
    certInfo.mCertURL = "test-cert-url";

    EXPECT_CALL(mCertProvider, GetCert)
        .WillOnce(Invoke([&certInfo](const String&, const Array<uint8_t>&, const Array<uint8_t>&, auto& out) {
            out = certInfo;

            return ErrorEnum::eNone;
        }));

    auto status = clientStub->GetCert(&context, request, &response);

    ASSERT_TRUE(status.ok()) << "GetCertSucceeds failed: code = " << status.error_code()
                             << ", message = " << status.error_message();

    EXPECT_EQ(response.type(), "test-type");
    EXPECT_EQ(response.key_url(), "test-key-url");
    EXPECT_EQ(response.cert_url(), "test-cert-url");
}

TEST_F(PublicMessageHandlerTest, GetCertFails)
{
    auto clientStub = CreateClientStub<iamproto::IAMPublicCertService>();
    ASSERT_NE(clientStub, nullptr) << "Failed to create client stub";

    grpc::ClientContext      context;
    iamproto::GetCertRequest request;
    iamproto::CertInfo       response;

    request.set_issuer("test-issuer");
    request.set_serial("58bdb46d06865f7f");
    request.set_type("test-type");

    CertInfo certInfo;
    certInfo.mKeyURL  = "test-key-url";
    certInfo.mCertURL = "test-cert-url";

    EXPECT_CALL(mCertProvider, GetCert)
        .WillOnce(Invoke([&certInfo](const String&, const Array<uint8_t>&, const Array<uint8_t>&, auto& out) {
            out = certInfo;

            return ErrorEnum::eFailed;
        }));

    auto status = clientStub->GetCert(&context, request, &response);

    ASSERT_FALSE(status.ok());
}

TEST_F(PublicMessageHandlerTest, SubscribeCertChangedSucceeds)
{
    auto clientStub = CreateClientStub<iamproto::IAMPublicCertService>();
    ASSERT_NE(clientStub, nullptr) << "Failed to create client stub";

    grpc::ClientContext                   context;
    iamproto::SubscribeCertChangedRequest request;
    iamanager::v6::CertInfo               response;

    request.set_type("test-type");

    CertInfo certInfo;
    certInfo.mKeyURL  = "test-key-url";
    certInfo.mCertURL = "test-cert-url";

    EXPECT_CALL(mCertProvider, SubscribeListener)
        .WillOnce(Invoke([&certInfo](const String&, iamclient::CertListenerItf& listener) {
            listener.OnCertChanged(certInfo);

            return ErrorEnum::eNone;
        }));

    auto reader = clientStub->SubscribeCertChanged(&context, request);

    ASSERT_TRUE(reader->Read(&response));
    EXPECT_EQ(response.type(), request.type());
    EXPECT_EQ(response.key_url(), certInfo.mKeyURL.CStr());
    EXPECT_EQ(response.cert_url(), certInfo.mCertURL.CStr());

    context.TryCancel();

    auto status = reader->Finish();

    ASSERT_EQ(status.error_code(), grpc::StatusCode::CANCELLED)
        << "Stream finish should return CANCELLED code: code = " << status.error_code()
        << ", message = " << status.error_message();
}

TEST_F(PublicMessageHandlerTest, SubscribeCertChangedFailed)
{
    auto clientStub = CreateClientStub<iamproto::IAMPublicCertService>();
    ASSERT_NE(clientStub, nullptr) << "Failed to create client stub";

    grpc::ClientContext                   context;
    iamproto::SubscribeCertChangedRequest request;
    iamanager::v6::CertInfo               response;

    request.set_type("test-type");

    EXPECT_CALL(mCertProvider, SubscribeListener).WillOnce(Invoke([](const String&, iamclient::CertListenerItf&) {
        return ErrorEnum::eFailed;
    }));

    auto reader = clientStub->SubscribeCertChanged(&context, request);

    ASSERT_FALSE(reader->Read(&response));

    context.TryCancel();

    auto status = reader->Finish();

    ASSERT_EQ(status.error_code(), grpc::StatusCode::CANCELLED)
        << "Stream finish should return CANCELLED code: code = " << status.error_code()
        << ", message = " << status.error_message();
}

/***********************************************************************************************************************
 * IAMPublicIdentityService tests
 **********************************************************************************************************************/

TEST_F(PublicMessageHandlerTest, GetSystemInfoSucceeds)
{
    auto clientStub = CreateClientStub<iamproto::IAMPublicIdentityService>();
    ASSERT_NE(clientStub, nullptr) << "Failed to create client stub";

    grpc::ClientContext     context;
    google::protobuf::Empty request;
    iamproto::SystemInfo    response;

    auto systemInfo        = std::make_unique<SystemInfo>();
    systemInfo->mSystemID  = cSystemID;
    systemInfo->mUnitModel = cUnitModel;

    EXPECT_CALL(mIdentProvider, GetSystemInfo).WillOnce(DoAll(SetArgReferee<0>(*systemInfo), Return(ErrorEnum::eNone)));

    const auto status = clientStub->GetSystemInfo(&context, request, &response);

    ASSERT_TRUE(status.ok()) << "GetSystemInfo failed: code = " << status.error_code()
                             << ", message = " << status.error_message();

    ASSERT_EQ(response.system_id(), cSystemID);
    ASSERT_EQ(response.unit_model(), cUnitModel);
}

TEST_F(PublicMessageHandlerTest, GetSystemInfoFails)
{
    auto clientStub = CreateClientStub<iamproto::IAMPublicIdentityService>();
    ASSERT_NE(clientStub, nullptr) << "Failed to create client stub";

    grpc::ClientContext     context;
    google::protobuf::Empty request;
    iamproto::SystemInfo    response;

    auto systemInfo        = std::make_unique<SystemInfo>();
    systemInfo->mSystemID  = cSystemID;
    systemInfo->mUnitModel = cUnitModel;

    EXPECT_CALL(mIdentProvider, GetSystemInfo)
        .WillOnce(DoAll(SetArgReferee<0>(*systemInfo), Return(ErrorEnum::eFailed)));

    const auto status = clientStub->GetSystemInfo(&context, request, &response);

    ASSERT_FALSE(status.ok());
}

TEST_F(PublicMessageHandlerTest, GetSubjectsSucceeds)
{
    StaticArray<StaticString<cIDLen>, 10> subjects;

    auto clientStub = CreateClientStub<iamproto::IAMPublicIdentityService>();
    ASSERT_NE(clientStub, nullptr) << "Failed to create client stub";

    grpc::ClientContext     context;
    google::protobuf::Empty request;
    iamproto::Subjects      response;

    EXPECT_CALL(mIdentProvider, GetSubjects).WillOnce(Invoke([&subjects](auto& out) {
        out = subjects;

        return ErrorEnum::eNone;
    }));

    const auto status = clientStub->GetSubjects(&context, request, &response);

    ASSERT_TRUE(status.ok()) << "GetSubjects failed: code = " << status.error_code()
                             << ", message = " << status.error_message();

    ASSERT_EQ(response.subjects_size(), subjects.Size());
}

TEST_F(PublicMessageHandlerTest, GetSubjectsFails)
{
    auto clientStub = CreateClientStub<iamproto::IAMPublicIdentityService>();
    ASSERT_NE(clientStub, nullptr) << "Failed to create client stub";

    grpc::ClientContext     context;
    google::protobuf::Empty request;
    iamproto::Subjects      response;

    EXPECT_CALL(mIdentProvider, GetSubjects).WillOnce(Return(ErrorEnum::eFailed));

    const auto status = clientStub->GetSubjects(&context, request, &response);

    ASSERT_FALSE(status.ok());
}

TEST_F(PublicMessageHandlerTest, SubscribeSubjectsChanged)
{
    const std::vector<std::string> cSubjects = {"subject1", "subject2", "subject3"};

    auto clientStub = CreateClientStub<iamproto::IAMPublicIdentityService>();
    ASSERT_NE(clientStub, nullptr) << "Failed to create client stub";

    grpc::ClientContext     context;
    google::protobuf::Empty request;
    iamproto::Subjects      response;

    const auto clientReader = clientStub->SubscribeSubjectsChanged(&context, request);
    ASSERT_NE(clientReader, nullptr) << "Failed to create client reader";

    StaticArray<StaticString<cIDLen>, 3> newSubjects;
    for (const auto& subject : cSubjects) {
        EXPECT_TRUE(newSubjects.PushBack(subject.c_str()).IsNone());
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));

    mPublicMessageHandler.SubjectsChanged(newSubjects);

    while (clientReader->Read(&response)) {
        ASSERT_EQ(cSubjects.size(), response.subjects_size());
        for (size_t i = 0; i < cSubjects.size(); i++) {
            ASSERT_EQ(cSubjects[i], response.subjects(i));
        }

        break;
    }

    context.TryCancel();

    auto status = clientReader->Finish();

    ASSERT_EQ(status.error_code(), grpc::StatusCode::CANCELLED)
        << "Stream finish should return CANCELLED code: code = " << status.error_code()
        << ", message = " << status.error_message();
}

/***********************************************************************************************************************
 * IAMPublicPermissionsService tests
 **********************************************************************************************************************/

TEST_F(PublicMessageHandlerTest, GetPermissionsSucceeds)
{
    auto clientStub = CreateClientStub<iamproto::IAMPublicPermissionsService>();
    ASSERT_NE(clientStub, nullptr) << "Failed to create client stub";

    grpc::ClientContext           context;
    iamproto::PermissionsRequest  request;
    iamproto::PermissionsResponse response;

    EXPECT_CALL(mPermHandler, GetPermissions).WillOnce(Return(ErrorEnum::eNone));

    const auto status = clientStub->GetPermissions(&context, request, &response);

    ASSERT_TRUE(status.ok()) << "GetPermissions failed: code = " << status.error_code()
                             << ", message = " << status.error_message();
}

TEST_F(PublicMessageHandlerTest, GetPermissionsFails)
{
    auto clientStub = CreateClientStub<iamproto::IAMPublicPermissionsService>();
    ASSERT_NE(clientStub, nullptr) << "Failed to create client stub";

    grpc::ClientContext           context;
    iamproto::PermissionsRequest  request;
    iamproto::PermissionsResponse response;

    EXPECT_CALL(mPermHandler, GetPermissions).WillOnce(Return(ErrorEnum::eFailed));

    const auto status = clientStub->GetPermissions(&context, request, &response);

    ASSERT_FALSE(status.ok());
}

/***********************************************************************************************************************
 * IAMPublicNodesService tests
 **********************************************************************************************************************/

TEST_F(PublicMessageHandlerTest, GetAllNodeIDsSucceeds)
{
    auto clientStub = CreateClientStub<iamproto::IAMPublicNodesService>();
    ASSERT_NE(clientStub, nullptr) << "Failed to create client stub";

    google::protobuf::Empty request;
    iamproto::NodesID       response;
    grpc::ClientContext     context;

    EXPECT_CALL(mNodeManager, GetAllNodeIDs).WillOnce(Return(ErrorEnum::eNone));

    auto status = clientStub->GetAllNodeIDs(&context, request, &response);

    ASSERT_TRUE(status.ok()) << "First GetAllNodeIDs failed: code = " << status.error_code()
                             << ", message = " << status.error_message();

    EXPECT_EQ(response.ids_size(), 0);

    StaticArray<StaticString<cIDLen>, cMaxNumNodes> nodeIDs;
    nodeIDs.PushBack("node0");
    nodeIDs.PushBack("node1");

    EXPECT_CALL(mNodeManager, GetAllNodeIDs).WillOnce(Invoke([&nodeIDs](Array<StaticString<cIDLen>>& out) {
        out = nodeIDs;

        return ErrorEnum::eNone;
    }));

    grpc::ClientContext context2;
    status = clientStub->GetAllNodeIDs(&context2, request, &response);

    ASSERT_TRUE(status.ok()) << "Second GetAllNodeIDs failed: code = " << status.error_code()
                             << ", message = " << status.error_message();

    ASSERT_EQ(response.ids_size(), nodeIDs.Size());
    for (size_t i = 0; i < nodeIDs.Size(); i++) {
        EXPECT_EQ(String(response.ids(i).c_str()), nodeIDs[i]);
    }
}

TEST_F(PublicMessageHandlerTest, GetAllNodeIDsFails)
{
    auto clientStub = CreateClientStub<iamproto::IAMPublicNodesService>();
    ASSERT_NE(clientStub, nullptr) << "Failed to create client stub";

    google::protobuf::Empty request;
    iamproto::NodesID       response;
    grpc::ClientContext     context;

    EXPECT_CALL(mNodeManager, GetAllNodeIDs).WillOnce(Return(ErrorEnum::eFailed));

    auto status = clientStub->GetAllNodeIDs(&context, request, &response);

    ASSERT_FALSE(status.ok());
}

TEST_F(PublicMessageHandlerTest, GetNodeInfoSucceeds)
{
    auto clientStub = CreateClientStub<iamproto::IAMPublicNodesService>();
    ASSERT_NE(clientStub, nullptr) << "Failed to create client stub";

    iamproto::GetNodeInfoRequest request;
    iamproto::NodeInfo           response;
    grpc::ClientContext          context;

    request.set_node_id("test-node-id");

    EXPECT_CALL(mNodeManager, GetNodeInfo).WillOnce(Invoke([](const String& nodeID, NodeInfo& nodeInfo) {
        nodeInfo.mNodeID = nodeID;
        nodeInfo.mTitle  = "test-title";

        return ErrorEnum::eNone;
    }));

    auto status = clientStub->GetNodeInfo(&context, request, &response);

    ASSERT_TRUE(status.ok()) << "GetNodeInfo failed: code = " << status.error_code()
                             << ", message = " << status.error_message();

    ASSERT_EQ(response.node_id(), "test-node-id");
    ASSERT_EQ(response.title(), "test-title");
}

TEST_F(PublicMessageHandlerTest, GetNodeInfoFails)
{
    auto clientStub = CreateClientStub<iamproto::IAMPublicNodesService>();
    ASSERT_NE(clientStub, nullptr) << "Failed to create client stub";

    iamproto::GetNodeInfoRequest request;
    iamproto::NodeInfo           response;
    grpc::ClientContext          context;

    EXPECT_CALL(mNodeManager, GetNodeInfo).WillOnce(Return(ErrorEnum::eFailed));

    auto status = clientStub->GetNodeInfo(&context, request, &response);

    ASSERT_FALSE(status.ok());
}

TEST_F(PublicMessageHandlerTest, SubscribeNodeChanged)
{
    auto clientStub = CreateClientStub<iamproto::IAMPublicNodesService>();
    ASSERT_NE(clientStub, nullptr) << "Failed to create client stub";

    google::protobuf::Empty request;
    grpc::ClientContext     context;

    auto stream = clientStub->SubscribeNodeChanged(&context, request);
    ASSERT_NE(stream, nullptr) << "Failed to create client stream";

    std::this_thread::sleep_for(std::chrono::seconds(1));

    NodeInfo nodeInfo;

    nodeInfo.mNodeID = "test-node-id";
    nodeInfo.mTitle  = "test-title";

    mPublicMessageHandler.OnNodeInfoChange(nodeInfo);

    iamproto::NodeInfo response;
    ASSERT_TRUE(stream->Read(&response));

    EXPECT_EQ(response.node_id(), "test-node-id");
    EXPECT_EQ(response.title(), "test-title");

    context.TryCancel();

    auto status = stream->Finish();

    ASSERT_EQ(status.error_code(), grpc::StatusCode::CANCELLED)
        << status.error_message() << " (" << status.error_code() << ")";

    LOG_DBG() << "SubscribeNodeChanged test finished";
}

} // namespace aos::iam::iamserver
