/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <thread>
#include <vector>

#include <gmock/gmock.h>

#include <aos/test/log.hpp>

#include <aos/common/crypto/cryptoprovider.hpp>
#include <aos/iam/certhandler.hpp>
#include <aos/iam/certmodules/pkcs11/pkcs11.hpp>
#include <utils/grpchelper.hpp>

#include "iamserver/publicmessagehandler.hpp"

#include "mocks/certprovidermock.hpp"
#include "mocks/identhandlermock.hpp"
#include "mocks/nodeinfoprovidermock.hpp"
#include "mocks/nodemanagermock.hpp"
#include "mocks/permhandlermock.hpp"
#include "mocks/provisionmanagermock.hpp"
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
    iam::identhandler::IdentHandlerMock         mIdentHandler;
    iam::permhandler::PermHandlerMock           mPermHandler;
    iam::nodeinfoprovider::NodeInfoProviderMock mNodeInfoProvider;
    iam::nodemanager::NodeManagerMock           mNodeManager;
    iam::certprovider::CertProviderMock         mCertProvider;
    iam::provisionmanager::ProvisionManagerMock mProvisionManager;

private:
    void SetUp() override;
    void TearDown() override;
};

void PublicMessageHandlerTest::SetUp()
{
    test::InitLog();

    EXPECT_CALL(mNodeInfoProvider, GetNodeInfo).WillRepeatedly(Invoke([&](NodeInfo& nodeInfo) {
        nodeInfo.mNodeID   = "node0";
        nodeInfo.mNodeType = "test-type";
        nodeInfo.mAttrs.PushBack({"MainNode", ""});

        LOG_DBG() << "NodeInfoProvider::GetNodeInfo: " << nodeInfo.mNodeID.CStr() << ", " << nodeInfo.mNodeType.CStr();

        return ErrorEnum::eNone;
    }));

    auto err = mPublicMessageHandler.Init(
        mNodeController, mIdentHandler, mPermHandler, mNodeInfoProvider, mNodeManager, mCertProvider);

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

    ASSERT_EQ(response.version(), 5);
}

/***********************************************************************************************************************
 * IAMPublicService tests
 **********************************************************************************************************************/

TEST_F(PublicMessageHandlerTest, GetNodeInfo)
{
    auto clientStub = CreateClientStub<iamproto::IAMPublicService>();
    ASSERT_NE(clientStub, nullptr) << "Failed to create client stub";

    grpc::ClientContext     context;
    google::protobuf::Empty request;
    iamproto::NodeInfo      response;

    const auto status = clientStub->GetNodeInfo(&context, request, &response);

    ASSERT_TRUE(status.ok()) << "GetNodeInfo failed: code = " << status.error_code()
                             << ", message = " << status.error_message();

    ASSERT_EQ(response.node_id(), "node0");
    ASSERT_EQ(response.node_type(), "test-type");
}

TEST_F(PublicMessageHandlerTest, GetCertSucceeds)
{
    auto clientStub = CreateClientStub<iamproto::IAMPublicService>();
    ASSERT_NE(clientStub, nullptr) << "Failed to create client stub";

    grpc::ClientContext      context;
    iamproto::GetCertRequest request;
    iamproto::CertInfo       response;

    request.set_issuer("test-issuer");
    request.set_serial("58bdb46d06865f7f");
    request.set_type("test-type");

    iam::certhandler::CertInfo certInfo;
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
    auto clientStub = CreateClientStub<iamproto::IAMPublicService>();
    ASSERT_NE(clientStub, nullptr) << "Failed to create client stub";

    grpc::ClientContext      context;
    iamproto::GetCertRequest request;
    iamproto::CertInfo       response;

    request.set_issuer("test-issuer");
    request.set_serial("58bdb46d06865f7f");
    request.set_type("test-type");

    iam::certhandler::CertInfo certInfo;
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
    auto clientStub = CreateClientStub<iamproto::IAMPublicService>();
    ASSERT_NE(clientStub, nullptr) << "Failed to create client stub";

    grpc::ClientContext                   context;
    iamproto::SubscribeCertChangedRequest request;
    iamanager::v5::CertInfo               response;

    request.set_type("test-type");

    iam::certhandler::CertInfo certInfo;
    certInfo.mKeyURL  = "test-key-url";
    certInfo.mCertURL = "test-cert-url";

    EXPECT_CALL(mCertProvider, SubscribeCertChanged)
        .WillOnce(Invoke([&certInfo](const String&, iam::certhandler::CertReceiverItf& receiver) {
            receiver.OnCertChanged(certInfo);

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
    auto clientStub = CreateClientStub<iamproto::IAMPublicService>();
    ASSERT_NE(clientStub, nullptr) << "Failed to create client stub";

    grpc::ClientContext                   context;
    iamproto::SubscribeCertChangedRequest request;
    iamanager::v5::CertInfo               response;

    request.set_type("test-type");

    EXPECT_CALL(mCertProvider, SubscribeCertChanged)
        .WillOnce(Invoke([](const String&, iam::certhandler::CertReceiverItf&) { return ErrorEnum::eFailed; }));

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

    EXPECT_CALL(mIdentHandler, GetSystemID).WillOnce(Return(RetWithError<StaticString<cSystemIDLen>>(cSystemID)));
    EXPECT_CALL(mIdentHandler, GetUnitModel).WillOnce(Return(RetWithError<StaticString<cUnitModelLen>>(cUnitModel)));

    const auto status = clientStub->GetSystemInfo(&context, request, &response);

    ASSERT_TRUE(status.ok()) << "GetSystemInfo failed: code = " << status.error_code()
                             << ", message = " << status.error_message();

    ASSERT_EQ(response.system_id(), cSystemID);
    ASSERT_EQ(response.unit_model(), cUnitModel);
}

TEST_F(PublicMessageHandlerTest, GetSystemInfoFailsOnSystemId)
{
    auto clientStub = CreateClientStub<iamproto::IAMPublicIdentityService>();
    ASSERT_NE(clientStub, nullptr) << "Failed to create client stub";

    grpc::ClientContext     context;
    google::protobuf::Empty request;
    iamproto::SystemInfo    response;

    EXPECT_CALL(mIdentHandler, GetSystemID)
        .WillOnce(Return(RetWithError<StaticString<cSystemIDLen>>("", ErrorEnum::eFailed)));
    EXPECT_CALL(mIdentHandler, GetUnitModel).Times(0);

    const auto status = clientStub->GetSystemInfo(&context, request, &response);

    ASSERT_FALSE(status.ok());
}

TEST_F(PublicMessageHandlerTest, GetSystemInfoFailsOnUnitModel)
{
    auto clientStub = CreateClientStub<iamproto::IAMPublicIdentityService>();
    ASSERT_NE(clientStub, nullptr) << "Failed to create client stub";

    grpc::ClientContext     context;
    google::protobuf::Empty request;
    iamproto::SystemInfo    response;

    EXPECT_CALL(mIdentHandler, GetSystemID).WillOnce(Return(RetWithError<StaticString<cSystemIDLen>>(cSystemID)));
    EXPECT_CALL(mIdentHandler, GetUnitModel)
        .WillOnce(Return(RetWithError<StaticString<cUnitModelLen>>("", ErrorEnum::eFailed)));

    const auto status = clientStub->GetSystemInfo(&context, request, &response);

    ASSERT_FALSE(status.ok());
}

TEST_F(PublicMessageHandlerTest, GetSubjectsSucceeds)
{
    StaticArray<StaticString<cSubjectIDLen>, 10> subjects;

    auto clientStub = CreateClientStub<iamproto::IAMPublicIdentityService>();
    ASSERT_NE(clientStub, nullptr) << "Failed to create client stub";

    grpc::ClientContext     context;
    google::protobuf::Empty request;
    iamproto::Subjects      response;

    EXPECT_CALL(mIdentHandler, GetSubjects).WillOnce(Invoke([&subjects](auto& out) {
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

    EXPECT_CALL(mIdentHandler, GetSubjects).WillOnce(Return(ErrorEnum::eFailed));

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

    StaticArray<StaticString<cSubjectIDLen>, 3> newSubjects;
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

    EXPECT_CALL(mNodeManager, GetAllNodeIds).WillOnce(Return(ErrorEnum::eNone));

    auto status = clientStub->GetAllNodeIDs(&context, request, &response);

    ASSERT_TRUE(status.ok()) << "First GetAllNodeIDs failed: code = " << status.error_code()
                             << ", message = " << status.error_message();

    EXPECT_EQ(response.ids_size(), 0);

    StaticArray<StaticString<cNodeIDLen>, cMaxNumNodes> nodeIDs;
    nodeIDs.PushBack("node0");
    nodeIDs.PushBack("node1");

    EXPECT_CALL(mNodeManager, GetAllNodeIds).WillOnce(Invoke([&nodeIDs](Array<StaticString<cNodeIDLen>>& out) {
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

    EXPECT_CALL(mNodeManager, GetAllNodeIds).WillOnce(Return(ErrorEnum::eFailed));

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
        nodeInfo.mName   = "test-name";

        return ErrorEnum::eNone;
    }));

    auto status = clientStub->GetNodeInfo(&context, request, &response);

    ASSERT_TRUE(status.ok()) << "GetNodeInfo failed: code = " << status.error_code()
                             << ", message = " << status.error_message();

    ASSERT_EQ(response.node_id(), "test-node-id");
    ASSERT_EQ(response.name(), "test-name");
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
    nodeInfo.mName   = "test-name";

    mPublicMessageHandler.OnNodeInfoChange(nodeInfo);

    iamproto::NodeInfo response;
    ASSERT_TRUE(stream->Read(&response));

    EXPECT_EQ(response.node_id(), "test-node-id");
    EXPECT_EQ(response.name(), "test-name");

    context.TryCancel();

    auto status = stream->Finish();

    ASSERT_EQ(status.error_code(), grpc::StatusCode::CANCELLED)
        << status.error_message() << " (" << status.error_code() << ")";

    LOG_DBG() << "SubscribeNodeChanged test finished";
}

} // namespace aos::iam::iamserver
