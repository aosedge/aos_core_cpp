/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <memory>

#include <core/common/tools/string.hpp>
#include <core/iam/certhandler/certhandler.hpp>

#include <common/logger/logmodule.hpp>
#include <common/pbconvert/iam.hpp>

#include "publicmessagehandler.hpp"

namespace aos::iam::iamserver {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error PublicMessageHandler::Init(NodeController& nodeController, iamclient::IdentProviderItf& identProvider,
    iam::permhandler::PermHandlerItf& permHandler, iam::nodeinfoprovider::NodeInfoProviderItf& nodeInfoProvider,
    iam::nodemanager::NodeManagerItf& nodeManager, iamclient::CertProviderItf& certProvider)
{
    LOG_DBG() << "Initialize message handler: handler=public";

    mNodeController   = &nodeController;
    mIdentProvider    = &identProvider;
    mPermHandler      = &permHandler;
    mNodeInfoProvider = &nodeInfoProvider;
    mNodeManager      = &nodeManager;
    mCertProvider     = &certProvider;

    if (auto err = mNodeInfoProvider->GetNodeInfo(mNodeInfo); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

void PublicMessageHandler::RegisterServices(grpc::ServerBuilder& builder)
{
    LOG_DBG() << "Register services" << Log::Field("handler", "public");

    builder.RegisterService(static_cast<iamanager::IAMVersionService::Service*>(this));
    builder.RegisterService(static_cast<iamproto::IAMPublicCurrentNodeService::Service*>(this));
    builder.RegisterService(static_cast<iamproto::IAMPublicCertService::Service*>(this));

    if (GetPermHandler() != nullptr) {
        builder.RegisterService(static_cast<iamproto::IAMPublicPermissionsService::Service*>(this));
    }

    if (iam::nodeinfoprovider::IsMainNode(mNodeInfo)) {
        if (GetIdentProvider() != nullptr) {
            builder.RegisterService(static_cast<iamproto::IAMPublicIdentityService::Service*>(this));
        }

        builder.RegisterService(static_cast<iamproto::IAMPublicNodesService::Service*>(this));
    }
}

void PublicMessageHandler::OnNodeInfoChange(const NodeInfo& info)
{
    const auto pbInfo = common::pbconvert::ConvertToProto(info);

    if (info.mNodeID == mNodeInfo.mNodeID) {
        mCurrentNodeChangedController.WriteToStreams(pbInfo);
    }

    mNodeChangedController.WriteToStreams(pbInfo);
}

void PublicMessageHandler::OnNodeRemoved(const String& nodeID)
{
    (void)nodeID;
}

void PublicMessageHandler::SubjectsChanged(const Array<StaticString<cIDLen>>& subjects)
{
    LOG_DBG() << "Process subjects changed" << Log::Field("count", subjects.Size());

    mSubjectsChangedController.WriteToStreams(common::pbconvert::ConvertToProto(subjects));
}

void PublicMessageHandler::Start()
{
    std::lock_guard lock {mMutex};

    mNodeChangedController.Start();
    mCurrentNodeChangedController.Start();
    mSubjectsChangedController.Start();
    mClose = false;
}

void PublicMessageHandler::Close()
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Close message handler: handler=public";

    mNodeChangedController.Close();
    mCurrentNodeChangedController.Close();
    mSubjectsChangedController.Close();

    {
        std::lock_guard certWritersLock {mCertWritersLock};

        for (auto& certWriter : mCertWriters) {
            certWriter->Close();
        }

        mCertWriters.clear();
    }

    mClose = true;
    mRetryCondVar.notify_one();
}

/***********************************************************************************************************************
 * Protected
 **********************************************************************************************************************/

Error PublicMessageHandler::SetNodeState(const std::string& nodeID, const NodeState& state, bool provisioned)
{
    if (ProcessOnThisNode(nodeID)) {
        if (auto err = mNodeInfoProvider->SetNodeState(state, provisioned); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    if (auto err = mNodeManager->SetNodeState(nodeID.empty() ? mNodeInfo.mNodeID : nodeID.c_str(), state, provisioned);
        !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

bool PublicMessageHandler::ProcessOnThisNode(const std::string& nodeID)
{
    return nodeID.empty() || String(nodeID.c_str()) == GetNodeInfo().mNodeID;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * IAMVersionService implementation
 **********************************************************************************************************************/

grpc::Status PublicMessageHandler::GetAPIVersion([[maybe_unused]] grpc::ServerContext* context,
    [[maybe_unused]] const google::protobuf::Empty* request, iamanager::APIVersion* response)
{
    LOG_DBG() << "Process get API version";

    response->set_version(cIamAPIVersion);

    return grpc::Status::OK;
}

/***********************************************************************************************************************
 * IAMPublicCurrentNodeService implementation
 **********************************************************************************************************************/

::grpc::Status PublicMessageHandler::GetCurrentNodeInfo([[maybe_unused]] ::grpc::ServerContext* context,
    [[maybe_unused]] const ::google::protobuf::Empty* request, ::iamanager::v6::NodeInfo* response)
{
    LOG_DBG() << "Process get current node info";

    *response = common::pbconvert::ConvertToProto(mNodeInfo);

    return grpc::Status::OK;
}

::grpc::Status PublicMessageHandler::SubscribeCurrentNodeChanged(::grpc::ServerContext* context,
    [[maybe_unused]] const ::google::protobuf::Empty* request, ::grpc::ServerWriter<::iamanager::v6::NodeInfo>* writer)
{
    LOG_DBG() << "Process subscribe current node changed";

    return mCurrentNodeChangedController.HandleStream(context, writer);
}

/***********************************************************************************************************************
 * IAMPublicCertService implementation
 **********************************************************************************************************************/

grpc::Status PublicMessageHandler::GetCert([[maybe_unused]] grpc::ServerContext* context,
    const iamproto::GetCertRequest* request, iamproto::CertInfo* response)
{
    LOG_DBG() << "Process get cert request: type=" << request->type().c_str()
              << ", serial=" << request->serial().c_str();

    response->set_type(request->type());

    auto issuer
        = Array<uint8_t> {reinterpret_cast<const uint8_t*>(request->issuer().c_str()), request->issuer().length()};

    StaticArray<uint8_t, crypto::cSerialNumSize> serial;

    auto err = String(request->serial().c_str()).HexToByteArray(serial);
    if (!err.IsNone()) {
        LOG_ERR() << "Failed to convert serial number: " << err;

        return common::pbconvert::ConvertAosErrorToGrpcStatus(err);
    }

    CertInfo certInfo;

    err = mCertProvider->GetCert(request->type().c_str(), issuer, serial, certInfo);
    if (!err.IsNone()) {
        LOG_ERR() << "Failed to get cert: " << err;

        return common::pbconvert::ConvertAosErrorToGrpcStatus(err);
    }

    response->set_key_url(certInfo.mKeyURL.CStr());
    response->set_cert_url(certInfo.mCertURL.CStr());

    return grpc::Status::OK;
}

grpc::Status PublicMessageHandler::SubscribeCertChanged([[maybe_unused]] grpc::ServerContext* context,
    const iamanager::v6::SubscribeCertChangedRequest* request, grpc::ServerWriter<iamanager::v6::CertInfo>* writer)
{
    LOG_DBG() << "Process subscribe cert changed: type=" << request->type().c_str();

    auto certWriter = std::make_shared<CertWriter>(request->type());

    {
        std::lock_guard lock {mCertWritersLock};

        mCertWriters.push_back(certWriter);
    }

    auto err = mCertProvider->SubscribeListener(request->type().c_str(), *certWriter);
    if (!err.IsNone()) {
        LOG_ERR() << "Failed to subscribe cert changed, err=" << err;

        return common::pbconvert::ConvertAosErrorToGrpcStatus(err);
    }

    auto status = certWriter->HandleStream(context, writer);

    err = mCertProvider->UnsubscribeListener(*certWriter);
    if (!err.IsNone()) {
        LOG_ERR() << "Failed to unsubscribe cert changed, err=" << err;

        return common::pbconvert::ConvertAosErrorToGrpcStatus(err);
    }

    {
        std::lock_guard lock {mCertWritersLock};

        auto iter = std::remove(mCertWriters.begin(), mCertWriters.end(), certWriter);
        mCertWriters.erase(iter, mCertWriters.end());
    }

    return status;
}

/***********************************************************************************************************************
 * IAMPublicIdentityService implementation
 **********************************************************************************************************************/

grpc::Status PublicMessageHandler::GetSystemInfo([[maybe_unused]] grpc::ServerContext* context,
    [[maybe_unused]] const google::protobuf::Empty* request, iamproto::SystemInfo* response)
{
    LOG_DBG() << "Process get system info";

    auto systemInfo = std::make_unique<SystemInfo>();

    if (!GetIdentProvider()) {
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, "ident provider is not available");
    }

    if (auto err = GetIdentProvider()->GetSystemInfo(*systemInfo); !err.IsNone()) {
        LOG_ERR() << "Failed to get system info" << Log::Field(err);

        return common::pbconvert::ConvertAosErrorToGrpcStatus(err);
    }

    response->set_system_id(systemInfo->mSystemID.CStr());
    response->set_unit_model(systemInfo->mUnitModel.CStr());
    response->set_version(systemInfo->mVersion.CStr());

    return grpc::Status::OK;
}

grpc::Status PublicMessageHandler::GetSubjects([[maybe_unused]] grpc::ServerContext* context,
    [[maybe_unused]] const google::protobuf::Empty* request, iamproto::Subjects* response)
{
    LOG_DBG() << "Process get subjects";

    StaticArray<StaticString<cIDLen>, cMaxNumSubjects> subjects;

    if (!GetIdentProvider()) {
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, "ident provider is not available");
    }

    if (auto err = GetIdentProvider()->GetSubjects(subjects); !err.IsNone()) {
        LOG_ERR() << "Failed to get subjects: " << err;

        return common::pbconvert::ConvertAosErrorToGrpcStatus(err);
    }

    for (const auto& subj : subjects) {
        response->add_subjects(subj.CStr());
    }

    return grpc::Status::OK;
}

grpc::Status PublicMessageHandler::SubscribeSubjectsChanged([[maybe_unused]] grpc::ServerContext* context,
    [[maybe_unused]] const google::protobuf::Empty* request, grpc::ServerWriter<iamproto::Subjects>* writer)
{
    LOG_DBG() << "Process subscribe subjects changed";

    return mSubjectsChangedController.HandleStream(context, writer);
}

/***********************************************************************************************************************
 * IAMPublicPermissionsService implementation
 **********************************************************************************************************************/

grpc::Status PublicMessageHandler::GetPermissions([[maybe_unused]] grpc::ServerContext* context,
    const iamproto::PermissionsRequest* request, iamproto::PermissionsResponse* response)
{
    LOG_DBG() << "Process get permissions: funcServerID=" << request->functional_server_id().c_str();

    InstanceIdent aosInstanceIdent;
    auto          aosInstancePerm = std::make_unique<StaticArray<FunctionPermissions, cFuncServiceMaxCount>>();

    if (auto err = GetPermHandler()->GetPermissions(
            request->secret().c_str(), request->functional_server_id().c_str(), aosInstanceIdent, *aosInstancePerm);
        !err.IsNone()) {
        LOG_ERR() << "Failed to get permissions: " << err;

        return common::pbconvert::ConvertAosErrorToGrpcStatus(err);
    }

    ::common::v2::InstanceIdent instanceIdent;
    iamproto::Permissions       permissions;

    instanceIdent.set_item_id(aosInstanceIdent.mItemID.CStr());
    instanceIdent.set_subject_id(aosInstanceIdent.mSubjectID.CStr());
    instanceIdent.set_instance(aosInstanceIdent.mInstance);

    for (const auto& [key, val] : *aosInstancePerm) {
        (*permissions.mutable_permissions())[key.CStr()] = val.CStr();
    }

    *response->mutable_instance()    = instanceIdent;
    *response->mutable_permissions() = permissions;

    return grpc::Status::OK;
}

/***********************************************************************************************************************
 * IAMPublicNodesService implementation
 **********************************************************************************************************************/

grpc::Status PublicMessageHandler::GetAllNodeIDs([[maybe_unused]] grpc::ServerContext* context,
    [[maybe_unused]] const google::protobuf::Empty* request, iamproto::NodesID* response)
{
    LOG_DBG() << "Public message handler. Process get all node IDs";

    StaticArray<StaticString<cIDLen>, cMaxNumNodes> nodeIDs;

    if (auto err = mNodeManager->GetAllNodeIDs(nodeIDs); !err.IsNone()) {
        LOG_ERR() << "Failed to get all node IDs: err=" << err;

        return common::pbconvert::ConvertAosErrorToGrpcStatus(err);
    }

    for (const auto& id : nodeIDs) {
        response->add_ids(id.CStr());
    }

    return grpc::Status::OK;
}

grpc::Status PublicMessageHandler::GetNodeInfo([[maybe_unused]] grpc::ServerContext* context,
    [[maybe_unused]] const iamproto::GetNodeInfoRequest* request, iamproto::NodeInfo* response)
{
    LOG_DBG() << "Process get node info: nodeID=" << request->node_id().c_str();

    auto nodeInfo = std::make_unique<NodeInfo>();

    if (auto err = mNodeManager->GetNodeInfo(request->node_id().c_str(), *nodeInfo); !err.IsNone()) {
        LOG_ERR() << "Failed to get node info: err=" << err;

        return common::pbconvert::ConvertAosErrorToGrpcStatus(err);
    }

    *response = common::pbconvert::ConvertToProto(*nodeInfo);

    return grpc::Status::OK;
}

grpc::Status PublicMessageHandler::SubscribeNodeChanged([[maybe_unused]] grpc::ServerContext* context,
    [[maybe_unused]] const google::protobuf::Empty* request, grpc::ServerWriter<iamproto::NodeInfo>* writer)
{
    LOG_DBG() << "Process subscribe node changed";

    return mNodeChangedController.HandleStream(context, writer);
}

grpc::Status PublicMessageHandler::RegisterNode(grpc::ServerContext*                        context,
    grpc::ServerReaderWriter<iamproto::IAMIncomingMessages, iamproto::IAMOutgoingMessages>* stream)
{
    LOG_DBG() << "Process register node: handler=public";

    return GetNodeController()->HandleRegisterNodeStream(cProvisioned, stream, context, GetNodeManager());
}

} // namespace aos::iam::iamserver
