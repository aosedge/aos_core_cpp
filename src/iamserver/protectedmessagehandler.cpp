/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <memory>

#include <aos/common/crypto.hpp>
#include <aos/common/cryptoutils.hpp>
#include <aos/common/tools/string.hpp>
#include <aos/common/types.hpp>
#include <aos/iam/certhandler.hpp>

#include "logger/logmodule.hpp"
#include "protectedmessagehandler.hpp"
#include "utils/convert.hpp"

/***********************************************************************************************************************
 * Constants
 **********************************************************************************************************************/

static const aos::Error cStreamNotFoundError = {aos::ErrorEnum::eNotFound, "Stream not found"};

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

aos::Error ProtectedMessageHandler::Init(NodeController& nodeController,
    aos::iam::identhandler::IdentHandlerItf& identHandler, aos::iam::permhandler::PermHandlerItf& permHandler,
    aos::iam::nodeinfoprovider::NodeInfoProviderItf& nodeInfoProvider,
    aos::iam::nodemanager::NodeManagerItf&           nodeManager,
    aos::iam::provisionmanager::ProvisionManagerItf& provisionManager)
{
    LOG_DBG() << "Initialize message handler: handler=protected";

    return PublicMessageHandler::Init(
        nodeController, identHandler, permHandler, nodeInfoProvider, nodeManager, provisionManager);
}

void ProtectedMessageHandler::RegisterServices(grpc::ServerBuilder& builder, bool provisionMode)
{
    LOG_DBG() << "Register services: handler=protected";

    PublicMessageHandler::RegisterServices(builder);

    if (GetPermHandler() != nullptr) {
        builder.RegisterService(static_cast<iamproto::IAMPermissionsService::Service*>(this));
    }

    if (aos::iam::nodeinfoprovider::IsMainNode(GetNodeInfo())) {
        builder.RegisterService(static_cast<iamproto::IAMCertificateService::Service*>(this));

        if (provisionMode) {
            builder.RegisterService(static_cast<iamproto::IAMProvisioningService::Service*>(this));
        }

        builder.RegisterService(static_cast<iamproto::IAMNodesService::Service*>(this));
    }
}

void ProtectedMessageHandler::Close()
{
    LOG_DBG() << "Close message handler: handler=protected";

    PublicMessageHandler::Close();
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * IAMPublicNodesService implementation
 **********************************************************************************************************************/

grpc::Status ProtectedMessageHandler::RegisterNode(grpc::ServerContext*                         context,
    grpc::ServerReaderWriter<::iamproto::IAMIncomingMessages, ::iamproto::IAMOutgoingMessages>* stream)
{
    LOG_DBG() << "Process register node: handler=protected";

    return GetNodeController()->HandleRegisterNodeStream(
        {cAllowedStatuses.cbegin(), cAllowedStatuses.cend()}, stream, context, GetNodeManager());
}

/***********************************************************************************************************************
 * IAMNodesService implementation
 **********************************************************************************************************************/

grpc::Status ProtectedMessageHandler::PauseNode([[maybe_unused]] grpc::ServerContext* context,
    const iamproto::PauseNodeRequest* request, iamproto::PauseNodeResponse* response)
{
    const auto& nodeID = request->node_id();

    LOG_DBG() << "Process pause node: nodeID=" << nodeID.c_str();

    if (!ProcessOnThisNode(nodeID)) {
        if (auto handler = GetNodeController()->GetNodeStreamHandler(nodeID); handler) {
            return handler->PauseNode(request, response, cDefaultTimeout);
        }

        LOG_ERR() << "Stream handler not found: nodeID=" << nodeID.c_str();

        return utils::ConvertAosErrorToGrpcStatus(cStreamNotFoundError);
    }

    if (auto err = SetNodeStatus(aos::NodeStatusEnum::ePaused); !err.IsNone()) {
        utils::SetErrorInfo(err, *response);
    }

    return grpc::Status::OK;
}

grpc::Status ProtectedMessageHandler::ResumeNode([[maybe_unused]] grpc::ServerContext* context,
    const iamproto::ResumeNodeRequest* request, iamproto::ResumeNodeResponse* response)
{
    const auto& nodeID = request->node_id();

    LOG_DBG() << "Process resume node: nodeID=" << nodeID.c_str();

    if (!ProcessOnThisNode(nodeID)) {
        if (auto handler = GetNodeController()->GetNodeStreamHandler(nodeID); handler) {
            return handler->ResumeNode(request, response, cDefaultTimeout);
        }

        LOG_ERR() << "Stream handler not found: nodeID=" << nodeID.c_str();

        return utils::ConvertAosErrorToGrpcStatus(cStreamNotFoundError);
    }

    if (auto err = SetNodeStatus(aos::NodeStatusEnum::eProvisioned); !err.IsNone()) {
        LOG_ERR() << "Failed to set node status error: " << err;

        utils::SetErrorInfo(err, *response);
    }

    return grpc::Status::OK;
}

/***********************************************************************************************************************
 * IAMProvisioningService implementation
 **********************************************************************************************************************/

grpc::Status ProtectedMessageHandler::GetCertTypes([[maybe_unused]] grpc::ServerContext* context,
    const iamproto::GetCertTypesRequest* request, iamproto::CertTypes* response)
{
    const auto& nodeID = request->node_id();

    LOG_DBG() << "Process get cert types: ID = " << nodeID.c_str();

    if (!ProcessOnThisNode(nodeID)) {
        if (auto handler = GetNodeController()->GetNodeStreamHandler(nodeID); handler) {
            return handler->GetCertTypes(request, response, cDefaultTimeout);
        }

        LOG_ERR() << "Stream handler not found: nodeID=" << nodeID.c_str();

        return utils::ConvertAosErrorToGrpcStatus(cStreamNotFoundError);
    }

    aos::Error                            err;
    aos::iam::provisionmanager::CertTypes certTypes;

    aos::Tie(certTypes, err) = GetProvisionManager()->GetCertTypes();
    if (!err.IsNone()) {
        LOG_ERR() << "Get certificate types error: " << AOS_ERROR_WRAP(err);

        return utils::ConvertAosErrorToGrpcStatus(cStreamNotFoundError);
    }

    utils::ConvertToProto(certTypes, *response->mutable_types());

    return grpc::Status::OK;
}

grpc::Status ProtectedMessageHandler::StartProvisioning([[maybe_unused]] grpc::ServerContext* context,
    const iamproto::StartProvisioningRequest* request, iamproto::StartProvisioningResponse* response)
{
    const auto& nodeID = request->node_id();

    LOG_DBG() << "Process start provisioning request: nodeID=" << nodeID.c_str();

    if (!ProcessOnThisNode(nodeID)) {
        if (auto handler = GetNodeController()->GetNodeStreamHandler(nodeID); handler) {
            return handler->StartProvisioning(request, response, cProvisioningTimeout);
        }

        LOG_ERR() << "Stream handler not found: nodeID=" << nodeID.c_str();

        return utils::ConvertAosErrorToGrpcStatus(cStreamNotFoundError);
    }

    if (auto err = GetProvisionManager()->StartProvisioning(request->password().c_str()); !err.IsNone()) {
        LOG_ERR() << "Start provisioning error: " << err;

        utils::SetErrorInfo(err, *response);
    }

    return grpc::Status::OK;
}

grpc::Status ProtectedMessageHandler::FinishProvisioning([[maybe_unused]] grpc::ServerContext* context,
    const iamproto::FinishProvisioningRequest* request, iamproto::FinishProvisioningResponse* response)
{
    const auto& nodeID = request->node_id();

    LOG_DBG() << "Process finish provisioning request: nodeID=" << nodeID.c_str();

    if (!ProcessOnThisNode(nodeID)) {
        if (auto handler = GetNodeController()->GetNodeStreamHandler(nodeID); handler) {
            return handler->FinishProvisioning(request, response, cProvisioningTimeout);
        }

        LOG_ERR() << "Stream handler not found: nodeID=" << nodeID.c_str();

        return utils::ConvertAosErrorToGrpcStatus(cStreamNotFoundError);
    }

    auto err = GetProvisionManager()->FinishProvisioning(request->password().c_str());
    if (!err.IsNone()) {
        LOG_ERR() << "Finish provisioning failed: " << err;

        utils::SetErrorInfo(err, *response);

        return grpc::Status::OK;
    }

    if (err = SetNodeStatus(aos::NodeStatusEnum::eProvisioned); !err.IsNone()) {
        LOG_ERR() << "Set node status failed: " << err;

        utils::SetErrorInfo(err, *response);

        return grpc::Status::OK;
    }

    return grpc::Status::OK;
}

grpc::Status ProtectedMessageHandler::Deprovision([[maybe_unused]] grpc::ServerContext* context,
    const iamproto::DeprovisionRequest* request, iamproto::DeprovisionResponse* response)
{
    const auto& nodeID = request->node_id();

    LOG_DBG() << "Process deprovision request: nodeID=" << nodeID.c_str();

    if (!ProcessOnThisNode(nodeID)) {
        if (auto handler = GetNodeController()->GetNodeStreamHandler(nodeID); handler) {
            return handler->Deprovision(request, response, cProvisioningTimeout);
        }

        LOG_ERR() << "Stream handler not found: nodeID=" << nodeID.c_str();

        return utils::ConvertAosErrorToGrpcStatus(cStreamNotFoundError);
    }

    if (auto err = GetProvisionManager()->Deprovision(request->password().c_str()); !err.IsNone()) {
        LOG_ERR() << "Deprovision failed: " << err;

        utils::SetErrorInfo(err, *response);

        return grpc::Status::OK;
    }

    if (auto err = SetNodeStatus(aos::NodeStatusEnum::eUnprovisioned); !err.IsNone()) {
        LOG_ERR() << "Set node status failed: " << err;

        utils::SetErrorInfo(err, *response);
    }

    return grpc::Status::OK;
}

/***********************************************************************************************************************
 * IAMCertificateService implementation
 **********************************************************************************************************************/

grpc::Status ProtectedMessageHandler::CreateKey([[maybe_unused]] grpc::ServerContext* context,
    const iamproto::CreateKeyRequest* request, iamproto::CreateKeyResponse* response)
{
    const auto& nodeID   = request->node_id();
    const auto  certType = aos::String(request->type().c_str());

    LOG_DBG() << "Process create key request: nodeID=" << nodeID.c_str() << ", type=" << certType;

    aos::StaticString<aos::cSystemIDLen> subject = request->subject().c_str();

    if (subject.IsEmpty() && !GetIdentHandler()) {
        aos::Error err(aos::ErrorEnum::eNotFound, "Subject can't be empty");

        LOG_ERR() << "Create key failed: " << err;

        utils::SetErrorInfo(err, *response);

        return grpc::Status::OK;
    }

    aos::Error err = aos::ErrorEnum::eNone;

    if (subject.IsEmpty() && GetIdentHandler()) {
        Tie(subject, err) = GetIdentHandler()->GetSystemID();
        if (!err.IsNone()) {
            LOG_ERR() << "Get system ID failed: " << err;

            utils::SetErrorInfo(err, *response);

            return grpc::Status::OK;
        }
    }

    if (!ProcessOnThisNode(nodeID)) {
        if (auto handler = GetNodeController()->GetNodeStreamHandler(nodeID); handler) {
            iamproto::CreateKeyRequest keyRequest = *request;
            keyRequest.set_subject(subject.CStr());

            return handler->CreateKey(&keyRequest, response, cDefaultTimeout);
        }

        LOG_ERR() << "Stream handler not found: nodeID=" << nodeID.c_str();

        return utils::ConvertAosErrorToGrpcStatus(cStreamNotFoundError);
    }

    const auto password = aos::String(request->password().c_str());

    aos::StaticString<aos::crypto::cCSRPEMLen> csr;

    if (err = GetProvisionManager()->CreateKey(certType, subject, password, csr); !err.IsNone()) {
        LOG_ERR() << "Create key failed: " << err;

        utils::SetErrorInfo(err, *response);

        return grpc::Status::OK;
    }

    response->set_node_id(nodeID);
    response->set_type(certType.CStr());
    response->set_csr(csr.CStr());

    return grpc::Status::OK;
}

grpc::Status ProtectedMessageHandler::ApplyCert([[maybe_unused]] grpc::ServerContext* context,
    const iamproto::ApplyCertRequest* request, iamproto::ApplyCertResponse* response)
{
    const auto& nodeID   = request->node_id();
    const auto  certType = aos::String(request->type().c_str());

    LOG_DBG() << "Process apply cert request: nodeID=" << nodeID.c_str() << ",type=" << certType;

    response->set_node_id(nodeID);
    response->set_type(certType.CStr());

    if (!ProcessOnThisNode(nodeID)) {
        if (auto handler = GetNodeController()->GetNodeStreamHandler(nodeID); handler) {
            return handler->ApplyCert(request, response, cDefaultTimeout);
        }

        LOG_ERR() << "Stream handler not found: nodeID=" << nodeID.c_str();

        return utils::ConvertAosErrorToGrpcStatus(cStreamNotFoundError);
    }

    const auto pemCert = aos::String(request->cert().c_str());

    aos::iam::certhandler::CertInfo certInfo;

    if (auto err = GetProvisionManager()->ApplyCert(certType, pemCert, certInfo); !err.IsNone()) {
        LOG_ERR() << "Apply cert failed: " << err;

        utils::SetErrorInfo(err, *response);

        return grpc::Status::OK;
    }

    aos::Error  err;
    std::string serial;

    Tie(serial, err) = utils::ConvertSerialToProto(certInfo.mSerial);
    if (!err.IsNone()) {
        LOG_ERR() << "Convert serial failed: " << err;

        utils::SetErrorInfo(err, *response);

        return grpc::Status::OK;
    }

    response->set_cert_url(certInfo.mCertURL.CStr());
    response->set_serial(serial);

    return grpc::Status::OK;
}

/***********************************************************************************************************************
 * IAMPermissionsService implementation
 **********************************************************************************************************************/

grpc::Status ProtectedMessageHandler::RegisterInstance([[maybe_unused]] grpc::ServerContext* context,
    const iamproto::RegisterInstanceRequest* request, iamproto::RegisterInstanceResponse* response)
{
    aos::Error err         = aos::ErrorEnum::eNone;
    const auto aosInstance = utils::ConvertToAos(request->instance());

    LOG_DBG() << "Process register instance: servicenodeID=" << aosInstance.mServiceID
              << ", subjectnodeID=" << aosInstance.mSubjectID << ", instance=" << aosInstance.mInstance;

    // Convert permissions
    aos::StaticArray<aos::iam::permhandler::FunctionalServicePermissions, aos::cMaxNumServices> aosPermissions;

    for (const auto& [service, permissions] : request->permissions()) {
        if (err = aosPermissions.PushBack({}); !err.IsNone()) {
            LOG_ERR() << "Failed to push back permissions: " << err;

            return utils::ConvertAosErrorToGrpcStatus(err);
        }

        aos::iam::permhandler::FunctionalServicePermissions& servicePerm = aosPermissions.Back().mValue;
        servicePerm.mName                                                = service.c_str();

        for (const auto& [key, val] : permissions.permissions()) {
            if (err = servicePerm.mPermissions.PushBack({key.c_str(), val.c_str()}); !err.IsNone()) {
                LOG_ERR() << "Failed to push back permissions: " << err;

                return utils::ConvertAosErrorToGrpcStatus(err);
            }
        }
    }

    aos::StaticString<aos::uuid::cUUIDLen> secret;

    Tie(secret, err) = GetPermHandler()->RegisterInstance(aosInstance, aosPermissions);

    if (!err.IsNone()) {
        LOG_ERR() << "Register instance failed: " << err;

        return utils::ConvertAosErrorToGrpcStatus(err);
    }

    response->set_secret(secret.CStr());

    return grpc::Status::OK;
}

grpc::Status ProtectedMessageHandler::UnregisterInstance([[maybe_unused]] grpc::ServerContext* context,
    const iamproto::UnregisterInstanceRequest* request, [[maybe_unused]] google::protobuf::Empty* response)
{
    const auto instance = utils::ConvertToAos(request->instance());

    LOG_DBG() << "Process unregister instance: servicenodeID=" << instance.mServiceID
              << ", subjectnodeID=" << instance.mSubjectID << ", instance=" << instance.mInstance;

    if (auto err = GetPermHandler()->UnregisterInstance(instance); !err.IsNone()) {
        LOG_ERR() << "Unregister instance failed: " << err;

        return utils::ConvertAosErrorToGrpcStatus(err);
    }

    return grpc::Status::OK;
}

bool ProtectedMessageHandler::ProcessOnThisNode(const std::string& nodeId)
{
    return nodeId.empty() || aos::String(nodeId.c_str()) == GetNodeInfo().mNodeID;
}
