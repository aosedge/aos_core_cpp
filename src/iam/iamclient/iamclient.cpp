/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <core/common/tools/logger.hpp>

#include <common/pbconvert/common.hpp>
#include <common/pbconvert/iam.hpp>
#include <common/utils/exception.hpp>

#include "iamclient.hpp"

namespace aos::iam::iamclient {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error IAMClient::Init(const config::IAMClientConfig& config, aos::iamclient::IdentProviderItf* identProvider,
    aos::iamclient::CertProviderItf& certProvider, provisionmanager::ProvisionManagerItf& provisionManager,
    common::iamclient::TLSCredentialsItf& tlsCredentials, currentnode::CurrentNodeHandlerItf& currentNodeHandler,
    bool provisioningMode)
{
    mIdentProvider      = identProvider;
    mCurrentNodeHandler = &currentNodeHandler;
    mCertProvider       = &certProvider;
    mProvisionManager   = &provisionManager;
    mCertStorage        = !provisioningMode ? config.mCertStorage : "";

    return PublicNodesService::Init(
        provisioningMode ? config.mMainIAMPublicServerURL : config.mMainIAMProtectedServerURL, tlsCredentials,
        provisioningMode, provisioningMode, mCertStorage);
}

Error IAMClient::Start()
{
    LOG_DBG() << "Start IAM client";

    if (!mCertStorage.empty()) {
        if (auto err = mCertProvider->SubscribeListener(String(mCertStorage.c_str()), *this); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    return PublicNodesService::Start();
}

Error IAMClient::Stop()
{
    LOG_DBG() << "Stop IAM client";

    PublicNodesService::Stop();

    if (!mCertStorage.empty()) {
        return AOS_ERROR_WRAP(mCertProvider->UnsubscribeListener(*this));
    }

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * Protected
 **********************************************************************************************************************/

Error IAMClient::ReceiveMessage(const iamanager::v6::IAMIncomingMessages& msg)
{
    if (msg.has_start_provisioning_request()) {
        return ProcessStartProvisioning(msg.start_provisioning_request());
    }

    if (msg.has_finish_provisioning_request()) {
        return ProcessFinishProvisioning(msg.finish_provisioning_request());
    }

    if (msg.has_deprovision_request()) {
        return ProcessDeprovision(msg.deprovision_request());
    }

    if (msg.has_pause_node_request()) {
        return ProcessPauseNode(msg.pause_node_request());
    }

    if (msg.has_resume_node_request()) {
        return ProcessResumeNode(msg.resume_node_request());
    }

    if (msg.has_create_key_request()) {
        return ProcessCreateKey(msg.create_key_request());
    }

    if (msg.has_apply_cert_request()) {
        return ProcessApplyCert(msg.apply_cert_request());
    }

    if (msg.has_get_cert_types_request()) {
        return ProcessGetCertTypes(msg.get_cert_types_request());
    }

    return AOS_ERROR_WRAP(ErrorEnum::eNotSupported);
}

void IAMClient::OnConnected()
{
    LOG_DBG() << "IAM client connected";

    mCurrentNodeHandler->SetConnected(true);

    if (auto err = SendNodeInfo(); !err.IsNone()) {
        LOG_ERR() << "Failed to send node info" << Log::Field(err);
    }
}

void IAMClient::OnDisconnected()
{
    LOG_DBG() << "IAM client disconnected";

    mCurrentNodeHandler->SetConnected(false);
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

void IAMClient::OnCertChanged([[maybe_unused]] const CertInfo& info)
{
    LOG_INF() << "Certificate changed, reconnecting";

    if (auto err = Reconnect(); !err.IsNone()) {
        LOG_ERR() << "Failed to reconnect" << Log::Field(err);
    }
}

Error IAMClient::SendNodeInfo()
{
    auto nodeInfo = std::make_unique<NodeInfo>();

    auto err = mCurrentNodeHandler->GetCurrentNodeInfo(*nodeInfo);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    iamanager::v6::IAMOutgoingMessages outgoingMsg;
    *outgoingMsg.mutable_node_info() = common::pbconvert::ConvertToProto(*nodeInfo);

    LOG_DBG() << "Send node info: state=" << nodeInfo->mState;

    return SendMessage(outgoingMsg);
}

Error IAMClient::ProcessStartProvisioning(const iamanager::v6::StartProvisioningRequest& request)
{
    LOG_DBG() << "Process start provisioning request";

    iamanager::v6::IAMOutgoingMessages outgoingMsg;
    auto&                              response = *outgoingMsg.mutable_start_provisioning_response();

    auto err = CheckCurrentNodeState({{NodeStateEnum::eUnprovisioned}});
    if (!err.IsNone()) {
        LOG_ERR() << "Can't start provisioning: wrong node state";

        common::pbconvert::SetErrorInfo(err, response);

        return SendMessage(outgoingMsg);
    }

    err = mProvisionManager->StartProvisioning(request.password().c_str());
    common::pbconvert::SetErrorInfo(err, response);

    return SendMessage(outgoingMsg);
}

Error IAMClient::ProcessFinishProvisioning(const iamanager::v6::FinishProvisioningRequest& request)
{
    LOG_DBG() << "Process finish provisioning request";

    iamanager::v6::IAMOutgoingMessages outgoingMsg;
    auto&                              response = *outgoingMsg.mutable_finish_provisioning_response();

    auto err = CheckCurrentNodeState({{NodeStateEnum::eUnprovisioned}});
    if (!err.IsNone()) {
        LOG_ERR() << "Can't finish provisioning: wrong node state";

        common::pbconvert::SetErrorInfo(err, response);

        return SendMessage(outgoingMsg);
    }

    err = mProvisionManager->FinishProvisioning(request.password().c_str());
    if (!err.IsNone()) {
        common::pbconvert::SetErrorInfo(err, response);

        return SendMessage(outgoingMsg);
    }

    err = mCurrentNodeHandler->SetState(NodeStateEnum::eProvisioned);
    if (!err.IsNone()) {
        common::pbconvert::SetErrorInfo(err, response);

        return SendMessage(outgoingMsg);
    }

    common::pbconvert::SetErrorInfo(err, response);

    return SendMessage(outgoingMsg);
}

Error IAMClient::ProcessDeprovision(const iamanager::v6::DeprovisionRequest& request)
{
    LOG_DBG() << "Process deprovision request";

    iamanager::v6::IAMOutgoingMessages outgoingMsg;
    auto&                              response = *outgoingMsg.mutable_deprovision_response();

    auto err = CheckCurrentNodeState({{NodeStateEnum::eProvisioned, NodeStateEnum::ePaused}});
    if (!err.IsNone()) {
        LOG_ERR() << "Can't deprovision: wrong node state";

        common::pbconvert::SetErrorInfo(err, response);

        return SendMessage(outgoingMsg);
    }

    err = mProvisionManager->Deprovision(request.password().c_str());
    if (!err.IsNone()) {
        common::pbconvert::SetErrorInfo(err, response);

        return SendMessage(outgoingMsg);
    }

    err = mCurrentNodeHandler->SetState(NodeStateEnum::eUnprovisioned);
    if (!err.IsNone()) {
        common::pbconvert::SetErrorInfo(err, response);

        return SendMessage(outgoingMsg);
    }

    common::pbconvert::SetErrorInfo(err, response);

    return SendMessage(outgoingMsg);
}

Error IAMClient::ProcessPauseNode(const iamanager::v6::PauseNodeRequest& request)
{
    LOG_DBG() << "Process pause node request";

    (void)request;

    iamanager::v6::IAMOutgoingMessages outgoingMsg;
    auto&                              response = *outgoingMsg.mutable_pause_node_response();

    auto err = CheckCurrentNodeState({{NodeStateEnum::eProvisioned}});
    if (!err.IsNone()) {
        LOG_ERR() << "Can't pause node: wrong node state";

        common::pbconvert::SetErrorInfo(err, response);

        return SendMessage(outgoingMsg);
    }

    err = mCurrentNodeHandler->SetState(NodeStateEnum::ePaused);
    if (!err.IsNone()) {
        common::pbconvert::SetErrorInfo(err, response);

        return SendMessage(outgoingMsg);
    }

    common::pbconvert::SetErrorInfo(err, response);

    err = SendNodeInfo();
    if (!err.IsNone()) {
        return err;
    }

    return SendMessage(outgoingMsg);
}

Error IAMClient::ProcessResumeNode(const iamanager::v6::ResumeNodeRequest& request)
{
    LOG_DBG() << "Process resume node request";

    (void)request;

    iamanager::v6::IAMOutgoingMessages outgoingMsg;
    auto&                              response = *outgoingMsg.mutable_resume_node_response();

    auto err = CheckCurrentNodeState({{NodeStateEnum::ePaused}});
    if (!err.IsNone()) {
        LOG_ERR() << "Can't resume node: wrong node state";

        common::pbconvert::SetErrorInfo(err, response);

        return SendMessage(outgoingMsg);
    }

    err = mCurrentNodeHandler->SetState(NodeStateEnum::eProvisioned);
    if (!err.IsNone()) {
        common::pbconvert::SetErrorInfo(err, response);

        return SendMessage(outgoingMsg);
    }

    common::pbconvert::SetErrorInfo(err, response);

    err = SendNodeInfo();
    if (!err.IsNone()) {
        return err;
    }

    return SendMessage(outgoingMsg);
}

Error IAMClient::ProcessCreateKey(const iamanager::v6::CreateKeyRequest& request)
{
    const String         nodeID   = request.node_id().c_str();
    const String         certType = request.type().c_str();
    StaticString<cIDLen> subject  = request.subject().c_str();
    const String         password = request.password().c_str();

    LOG_DBG() << "Process create key request: type=" << certType << ", subject=" << subject;

    if (subject.IsEmpty() && !mIdentProvider) {
        LOG_ERR() << "Subject can't be empty";

        return SendCreateKeyResponse(nodeID, certType, {}, AOS_ERROR_WRAP(ErrorEnum::eInvalidArgument));
    }

    Error err = ErrorEnum::eNone;

    if (subject.IsEmpty() && mIdentProvider) {
        auto systemInfo = std::make_unique<SystemInfo>();

        err = mIdentProvider->GetSystemInfo(*systemInfo);
        if (!err.IsNone()) {
            LOG_ERR() << "Getting system ID error: error=" << AOS_ERROR_WRAP(err);

            return SendCreateKeyResponse(nodeID, certType, {}, AOS_ERROR_WRAP(err));
        }

        subject = systemInfo->mSystemID;
    }

    auto csr = std::make_unique<StaticString<crypto::cCSRPEMLen>>();

    err = AOS_ERROR_WRAP(mProvisionManager->CreateKey(certType, subject, password, *csr));

    return SendCreateKeyResponse(nodeID, certType, *csr, err);
}

Error IAMClient::ProcessApplyCert(const iamanager::v6::ApplyCertRequest& request)
{
    const String nodeID   = request.node_id().c_str();
    const String certType = request.type().c_str();
    const String pemCert  = request.cert().c_str();

    LOG_DBG() << "Process apply cert request: type=" << certType;

    CertInfo certInfo;
    Error    err = AOS_ERROR_WRAP(mProvisionManager->ApplyCert(certType, pemCert, certInfo));

    return SendApplyCertResponse(nodeID, certType, certInfo.mCertURL, certInfo.mSerial, err);
}

Error IAMClient::ProcessGetCertTypes(const iamanager::v6::GetCertTypesRequest& request)
{
    const String nodeID = request.node_id().c_str();

    LOG_DBG() << "Process get cert types: nodeID=" << nodeID;

    auto [certTypes, err] = mProvisionManager->GetCertTypes();
    if (!err.IsNone()) {
        LOG_ERR() << "Get certificate types failed: error=" << AOS_ERROR_WRAP(err);
    }

    return SendGetCertTypesResponse(certTypes, err);
}

Error IAMClient::CheckCurrentNodeState(const std::optional<std::initializer_list<NodeState>>& allowedStates)
{
    auto nodeInfo = std::make_unique<NodeInfo>();

    auto err = mCurrentNodeHandler->GetCurrentNodeInfo(*nodeInfo);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (!allowedStates) {
        return ErrorEnum::eNone;
    }

    const bool isAllowed = std::any_of(allowedStates->begin(), allowedStates->end(),
        [currentState = nodeInfo->mState](const NodeState state) { return currentState == state; });

    return !isAllowed ? AOS_ERROR_WRAP(ErrorEnum::eWrongState) : ErrorEnum::eNone;
}

Error IAMClient::SendCreateKeyResponse(const String& nodeID, const String& type, const String& csr, const Error& error)
{
    iamanager::v6::IAMOutgoingMessages outgoingMsg;
    auto&                              response = *outgoingMsg.mutable_create_key_response();

    response.set_node_id(nodeID.CStr());
    response.set_type(type.CStr());
    response.set_csr(csr.CStr());

    common::pbconvert::SetErrorInfo(error, response);

    return SendMessage(outgoingMsg);
}

Error IAMClient::SendApplyCertResponse(
    const String& nodeID, const String& type, const String& certURL, const Array<uint8_t>& serial, const Error& error)
{
    iamanager::v6::IAMOutgoingMessages outgoingMsg;
    auto&                              response = *outgoingMsg.mutable_apply_cert_response();

    std::string protoSerial;
    Error       resultError = error;
    if (error.IsNone()) {
        Tie(protoSerial, resultError) = common::pbconvert::ConvertSerialToProto(serial);
        if (!resultError.IsNone()) {
            resultError = AOS_ERROR_WRAP(resultError);

            LOG_ERR() << "Serial conversion problem: error=" << resultError;
        }
    }

    response.set_node_id(nodeID.CStr());

    response.mutable_cert_info()->set_type(type.CStr());
    response.mutable_cert_info()->set_cert_url(certURL.CStr());
    response.mutable_cert_info()->set_serial(protoSerial);

    common::pbconvert::SetErrorInfo(error, response);

    return SendMessage(outgoingMsg);
}

Error IAMClient::SendGetCertTypesResponse(const provisionmanager::CertTypes& types, const Error& error)
{
    (void)error;

    iamanager::v6::IAMOutgoingMessages outgoingMsg;
    auto&                              response = *outgoingMsg.mutable_cert_types_response();

    for (const auto& type : types) {
        response.mutable_types()->Add(type.CStr());
    }

    return SendMessage(outgoingMsg);
}

} // namespace aos::iam::iamclient
