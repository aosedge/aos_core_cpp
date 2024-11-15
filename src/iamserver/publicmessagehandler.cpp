/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <memory>

#include <aos/common/crypto/crypto.hpp>
#include <aos/common/crypto/utils.hpp>
#include <aos/common/tools/string.hpp>
#include <aos/common/types.hpp>
#include <aos/iam/certhandler.hpp>

#include "logger/logmodule.hpp"
#include "publicmessagehandler.hpp"
#include "utils/convert.hpp"

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

aos::Error PublicMessageHandler::Init(NodeController& nodeController,
    aos::iam::identhandler::IdentHandlerItf& identHandler, aos::iam::permhandler::PermHandlerItf& permHandler,
    aos::iam::nodeinfoprovider::NodeInfoProviderItf& nodeInfoProvider,
    aos::iam::nodemanager::NodeManagerItf&           nodeManager,
    aos::iam::provisionmanager::ProvisionManagerItf& provisionManager)
{
    LOG_DBG() << "Initialize message handler: handler=public";

    mNodeController   = &nodeController;
    mIdentHandler     = &identHandler;
    mPermHandler      = &permHandler;
    mNodeInfoProvider = &nodeInfoProvider;
    mNodeManager      = &nodeManager;
    mProvisionManager = &provisionManager;

    if (auto err = mNodeInfoProvider->GetNodeInfo(mNodeInfo); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return aos::ErrorEnum::eNone;
}

void PublicMessageHandler::RegisterServices(grpc::ServerBuilder& builder)
{
    LOG_DBG() << "Register services: handler=public";

    builder.RegisterService(static_cast<iamanager::IAMVersionService::Service*>(this));
    builder.RegisterService(static_cast<iamproto::IAMPublicService::Service*>(this));

    if (GetPermHandler() != nullptr) {
        builder.RegisterService(static_cast<iamproto::IAMPublicPermissionsService::Service*>(this));
    }

    if (aos::iam::nodeinfoprovider::IsMainNode(mNodeInfo)) {
        if (GetIdentHandler() != nullptr) {
            builder.RegisterService(static_cast<iamproto::IAMPublicIdentityService::Service*>(this));
        }

        builder.RegisterService(static_cast<iamproto::IAMPublicNodesService::Service*>(this));
    }
}

void PublicMessageHandler::OnNodeInfoChange(const aos::NodeInfo& info)
{
    iamproto::NodeInfo nodeInfo;
    utils::ConvertToProto(info, nodeInfo);

    mNodeChangedController.WriteToStreams(nodeInfo);
}

void PublicMessageHandler::OnNodeRemoved(const aos::String& nodeID)
{
    (void)nodeID;
}

aos::Error PublicMessageHandler::SubjectsChanged(const aos::Array<aos::StaticString<aos::cSubjectIDLen>>& messages)
{
    LOG_DBG() << "Process subjects changed";

    iamproto::Subjects subjects;
    utils::ConvertToProto(messages, subjects);

    mSubjectsChangedController.WriteToStreams(subjects);

    return aos::ErrorEnum::eNone;
}

void PublicMessageHandler::Start()
{
    mNodeChangedController.Start();
    mSubjectsChangedController.Start();
}

void PublicMessageHandler::Close()
{
    LOG_DBG() << "Close message handler: handler=public";

    mNodeChangedController.Close();
    mSubjectsChangedController.Close();

    {
        std::lock_guard lock {mCertWritersLock};

        for (auto& certWriter : mCertWriters) {
            certWriter->Close();
        }

        mCertWriters.clear();
    }
}

/***********************************************************************************************************************
 * Protected
 **********************************************************************************************************************/

aos::Error PublicMessageHandler::SetNodeStatus(const std::string& nodeID, const aos::NodeStatus& status)
{
    if (ProcessOnThisNode(nodeID)) {
        if (auto err = mNodeInfoProvider->SetNodeStatus(status); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    if (auto err = mNodeManager->SetNodeStatus(nodeID.empty() ? mNodeInfo.mNodeID : nodeID.c_str(), status);
        !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return aos::ErrorEnum::eNone;
}

bool PublicMessageHandler::ProcessOnThisNode(const std::string& nodeID)
{
    return nodeID.empty() || aos::String(nodeID.c_str()) == GetNodeInfo().mNodeID;
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
 * IAMPublicService implementation
 **********************************************************************************************************************/

grpc::Status PublicMessageHandler::GetNodeInfo([[maybe_unused]] grpc::ServerContext* context,
    [[maybe_unused]] const google::protobuf::Empty* request, iamproto::NodeInfo* response)
{
    LOG_DBG() << "Process get node info";

    utils::ConvertToProto(mNodeInfo, *response);

    return grpc::Status::OK;
}

grpc::Status PublicMessageHandler::GetCert([[maybe_unused]] grpc::ServerContext* context,
    const iamproto::GetCertRequest* request, iamproto::CertInfo* response)
{
    LOG_DBG() << "Process get cert request: type=" << request->type().c_str()
              << ", serial=" << request->serial().c_str();

    response->set_type(request->type());

    auto issuer = utils::ConvertByteArrayToAos(request->issuer());

    aos::StaticArray<uint8_t, aos::crypto::cSerialNumSize> serial;

    auto err = aos::String(request->serial().c_str()).HexToByteArray(serial);
    if (!err.IsNone()) {
        LOG_ERR() << "Failed to convert serial number: " << err;

        return utils::ConvertAosErrorToGrpcStatus(err);
    }

    aos::iam::certhandler::CertInfo certInfo;

    err = GetProvisionManager()->GetCert(request->type().c_str(), issuer, serial, certInfo);
    if (!err.IsNone()) {
        LOG_ERR() << "Failed to get cert: " << err;

        return utils::ConvertAosErrorToGrpcStatus(err);
    }

    response->set_key_url(certInfo.mKeyURL.CStr());
    response->set_cert_url(certInfo.mCertURL.CStr());

    return grpc::Status::OK;
}

grpc::Status PublicMessageHandler::SubscribeCertChanged([[maybe_unused]] grpc::ServerContext* context,
    const iamanager::v5::SubscribeCertChangedRequest* request, grpc::ServerWriter<iamanager::v5::CertInfo>* writer)
{
    LOG_DBG() << "Process subscribe cert changed: type=" << request->type().c_str();

    auto certWriter = std::make_shared<CertWriter>(request->type());

    {
        std::lock_guard lock {mCertWritersLock};

        mCertWriters.push_back(certWriter);
    }

    auto err = GetProvisionManager()->SubscribeCertChanged(request->type().c_str(), *certWriter);
    if (!err.IsNone()) {
        LOG_ERR() << "Failed to subscribe cert changed, err=" << err;

        return utils::ConvertAosErrorToGrpcStatus(err);
    }

    auto status = certWriter->HandleStream(context, writer);

    err = GetProvisionManager()->UnsubscribeCertChanged(*certWriter);
    if (!err.IsNone()) {
        LOG_ERR() << "Failed to unsubscribe cert changed, err=" << err;

        return utils::ConvertAosErrorToGrpcStatus(err);
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

    aos::StaticString<aos::cSystemIDLen> systemID;
    aos::Error                           err;

    aos::Tie(systemID, err) = GetIdentHandler()->GetSystemID();
    if (!err.IsNone()) {
        LOG_ERR() << "Failed to get system ID: " << err;

        return utils::ConvertAosErrorToGrpcStatus(err);
    }

    aos::StaticString<aos::cUnitModelLen> boardModel;

    aos::Tie(boardModel, err) = GetIdentHandler()->GetUnitModel();
    if (!err.IsNone()) {
        LOG_ERR() << "Failed to get unit model: " << err;

        return utils::ConvertAosErrorToGrpcStatus(err);
    }

    response->set_system_id(systemID.CStr());
    response->set_unit_model(boardModel.CStr());

    return grpc::Status::OK;
}

grpc::Status PublicMessageHandler::GetSubjects([[maybe_unused]] grpc::ServerContext* context,
    [[maybe_unused]] const google::protobuf::Empty* request, iamproto::Subjects* response)
{
    LOG_DBG() << "Process get subjects";

    aos::StaticArray<aos::StaticString<aos::cSubjectIDLen>, aos::cMaxSubjectIDSize> subjects;

    if (auto err = GetIdentHandler()->GetSubjects(subjects); !err.IsNone()) {
        LOG_ERR() << "Failed to get subjects: " << err;

        return utils::ConvertAosErrorToGrpcStatus(err);
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

    aos::InstanceIdent aosInstanceIdent;
    aos::StaticArray<aos::iam::permhandler::PermKeyValue, aos::iam::permhandler::cServicePermissionMaxCount>
        aosInstancePerm;

    if (auto err = GetPermHandler()->GetPermissions(
            request->secret().c_str(), request->functional_server_id().c_str(), aosInstanceIdent, aosInstancePerm);
        !err.IsNone()) {
        LOG_ERR() << "Failed to get permissions: " << err;

        return utils::ConvertAosErrorToGrpcStatus(err);
    }

    common::v1::InstanceIdent instanceIdent;
    iamproto::Permissions     permissions;

    instanceIdent.set_service_id(aosInstanceIdent.mServiceID.CStr());
    instanceIdent.set_subject_id(aosInstanceIdent.mSubjectID.CStr());
    instanceIdent.set_instance(aosInstanceIdent.mInstance);

    for (const auto& [key, val] : aosInstancePerm) {
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

    aos::StaticArray<aos::StaticString<aos::cNodeIDLen>, aos::cMaxNumNodes> nodeIDs;

    if (auto err = mNodeManager->GetAllNodeIds(nodeIDs); !err.IsNone()) {
        LOG_ERR() << "Failed to get all node IDs: " << err;

        return utils::ConvertAosErrorToGrpcStatus(err);
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

    aos::NodeInfo nodeInfo;

    if (auto err = mNodeManager->GetNodeInfo(request->node_id().c_str(), nodeInfo); !err.IsNone()) {
        LOG_ERR() << "Failed to get node info: " << err;

        return utils::ConvertAosErrorToGrpcStatus(err);
    }

    utils::ConvertToProto(nodeInfo, *response);

    return grpc::Status::OK;
}

grpc::Status PublicMessageHandler::SubscribeNodeChanged([[maybe_unused]] grpc::ServerContext* context,
    [[maybe_unused]] const google::protobuf::Empty* request, grpc::ServerWriter<iamproto::NodeInfo>* writer)
{
    LOG_DBG() << "Process subscribe node changed";

    return mNodeChangedController.HandleStream(context, writer);
}

grpc::Status PublicMessageHandler::RegisterNode(grpc::ServerContext*                            context,
    grpc::ServerReaderWriter<::iamproto::IAMIncomingMessages, ::iamproto::IAMOutgoingMessages>* stream)
{
    LOG_DBG() << "Process register node: handler=public";

    return GetNodeController()->HandleRegisterNodeStream(
        {cAllowedStatuses.cbegin(), cAllowedStatuses.cend()}, stream, context, GetNodeManager());
}
