/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <Poco/Pipe.h>
#include <Poco/PipeStream.h>
#include <Poco/Process.h>
#include <Poco/StreamCopier.h>

#include <pbconvert/common.hpp>
#include <pbconvert/iam.hpp>
#include <utils/exception.hpp>
#include <utils/grpchelper.hpp>

#include "iamclient.hpp"
#include "logger/logmodule.hpp"

namespace aos::iam::iamclient {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error IAMClient::Init(const iam::config::Config& config, iam::identhandler::IdentHandlerItf* identHandler,
    iam::certprovider::CertProviderItf& certProvider, iam::provisionmanager::ProvisionManagerItf& provisionManager,
    crypto::CertLoaderItf& certLoader, crypto::x509::ProviderItf& cryptoProvider,
    iam::nodeinfoprovider::NodeInfoProviderItf& nodeInfoProvider, bool provisioningMode)
{
    mIdentHandler     = identHandler;
    mNodeInfoProvider = &nodeInfoProvider;
    mCertProvider     = &certProvider;
    mCertLoader       = &certLoader;
    mCryptoProvider   = &cryptoProvider;
    mProvisionManager = &provisionManager;

    mStartProvisioningCmdArgs  = config.mStartProvisioningCmdArgs;
    mDiskEncryptionCmdArgs     = config.mDiskEncryptionCmdArgs;
    mFinishProvisioningCmdArgs = config.mFinishProvisioningCmdArgs;
    mDeprovisionCmdArgs        = config.mDeprovisionCmdArgs;
    mReconnectInterval         = config.mNodeReconnectInterval;
    mCACert                    = config.mCACert;

    if (provisioningMode) {
        mCredentialList.push_back(grpc::InsecureChannelCredentials());
        if (!config.mCACert.empty()) {
            mCredentialList.push_back(common::utils::GetTLSClientCredentials(config.mCACert.c_str()));
        }

        mServerURL = config.mMainIAMPublicServerURL;
    } else {
        iam::certhandler::CertInfo certInfo;

        auto err = mCertProvider->GetCert(String(config.mCertStorage.c_str()), {}, {}, certInfo);
        if (!err.IsNone()) {
            LOG_ERR() << "Get certificates failed: error=" << err.Message();

            return AOS_ERROR_WRAP(ErrorEnum::eInvalidArgument);
        }

        err = mCertProvider->SubscribeCertChanged(String(config.mCertStorage.c_str()), *this);
        if (!err.IsNone()) {
            LOG_ERR() << "Subscribe certificate receiver failed: error=" << err.Message();

            return AOS_ERROR_WRAP(ErrorEnum::eInvalidArgument);
        }

        mCredentialList.push_back(
            common::utils::GetMTLSClientCredentials(certInfo, config.mCACert.c_str(), certLoader, cryptoProvider));

        mServerURL = config.mMainIAMProtectedServerURL;
    }

    mConnectionThread = std::thread(&IAMClient::ConnectionLoop, this);

    return ErrorEnum::eNone;
}

IAMClient::~IAMClient()
{
    {
        std::unique_lock lock {mShutdownLock};

        mShutdown = true;
        mShutdownCV.notify_all();

        mCertProvider->UnsubscribeCertChanged(*this);

        if (mRegisterNodeCtx) {
            mRegisterNodeCtx->TryCancel();
        }
    }

    if (mConnectionThread.joinable()) {
        mConnectionThread.join();
    }
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

void IAMClient::OnCertChanged(const iam::certhandler::CertInfo& info)
{
    std::unique_lock lock {mShutdownLock};

    mCredentialList.clear();
    mCredentialList.push_back(
        common::utils::GetMTLSClientCredentials(info, mCACert.c_str(), *mCertLoader, *mCryptoProvider));

    mCredentialListUpdated = true;
}

std::unique_ptr<grpc::ClientContext> IAMClient::CreateClientContext()
{
    return std::make_unique<grpc::ClientContext>();
}

PublicNodeServiceStubPtr IAMClient::CreateStub(
    const std::string& url, const std::shared_ptr<grpc::ChannelCredentials>& credentials)
{
    auto channel = grpc::CreateCustomChannel(url, credentials, grpc::ChannelArguments());
    if (!channel) {
        LOG_ERR() << "Can't create client channel";

        return nullptr;
    }

    return PublicNodeService::NewStub(channel);
}

bool IAMClient::RegisterNode(const std::string& url)
{
    std::unique_lock lock {mShutdownLock};

    for (const auto& credentials : mCredentialList) {
        if (mShutdown) {
            return false;
        }

        mPublicNodeServiceStub = CreateStub(url, credentials);
        if (!mPublicNodeServiceStub) {
            LOG_ERR() << "Stub is not created";

            continue;
        }

        mRegisterNodeCtx = CreateClientContext();
        mStream          = mPublicNodeServiceStub->RegisterNode(mRegisterNodeCtx.get());
        if (!mStream) {
            LOG_ERR() << "Stream creation problem";

            continue;
        }

        if (!SendNodeInfo()) {
            LOG_WRN() << "Connection failed with provided credentials";

            continue;
        }

        LOG_DBG() << "Connection established";

        mCredentialListUpdated = false;

        return true;
    }

    return false;
}

void IAMClient::ConnectionLoop() noexcept
{
    LOG_DBG() << "IAMClient connection thread started";

    while (true) {
        LOG_DBG() << "Connecting to IAMServer...";

        if (RegisterNode(mServerURL)) {
            HandleIncomingMessages();

            LOG_DBG() << "IAMClient connection closed";
        }

        std::unique_lock lock {mShutdownLock};

        mShutdownCV.wait_for(
            lock, std::chrono::nanoseconds(mReconnectInterval.Nanoseconds()), [this]() { return mShutdown; });
        if (mShutdown) {
            break;
        }
    }

    LOG_DBG() << "IAMClient connection thread stopped";
}

void IAMClient::HandleIncomingMessages() noexcept
{
    try {
        iamanager::v5::IAMIncomingMessages incomingMsg;

        while (mStream->Read(&incomingMsg)) {
            bool ok = true;

            if (incomingMsg.has_start_provisioning_request()) {
                ok = ProcessStartProvisioning(incomingMsg.start_provisioning_request());
            } else if (incomingMsg.has_finish_provisioning_request()) {
                ok = ProcessFinishProvisioning(incomingMsg.finish_provisioning_request());
            } else if (incomingMsg.has_deprovision_request()) {
                ok = ProcessDeprovision(incomingMsg.deprovision_request());
            } else if (incomingMsg.has_pause_node_request()) {
                ok = ProcessPauseNode(incomingMsg.pause_node_request());
            } else if (incomingMsg.has_resume_node_request()) {
                ok = ProcessResumeNode(incomingMsg.resume_node_request());
            } else if (incomingMsg.has_create_key_request()) {
                ok = ProcessCreateKey(incomingMsg.create_key_request());
            } else if (incomingMsg.has_apply_cert_request()) {
                ok = ProcessApplyCert(incomingMsg.apply_cert_request());
            } else if (incomingMsg.has_get_cert_types_request()) {
                ok = ProcessGetCertTypes(incomingMsg.get_cert_types_request());
            } else {
                AOS_ERROR_CHECK_AND_THROW("Not supported request type", ErrorEnum::eNotSupported);
            }

            if (!ok) {
                break;
            }

            {
                std::unique_lock lock {mShutdownLock};

                if (mCredentialListUpdated) {
                    LOG_DBG() << "Credential list updated: closing connection";

                    mRegisterNodeCtx->TryCancel();

                    break;
                }
            }
        }
    } catch (const std::exception& e) {
        LOG_ERR() << "Failed to handle incoming message: err=" << common::utils::ToAosError(e);
    }
}

bool IAMClient::SendNodeInfo()
{
    auto                               nodeInfo = std::make_unique<NodeInfo>();
    iamanager::v5::IAMOutgoingMessages outgoingMsg;

    auto err = mNodeInfoProvider->GetNodeInfo(*nodeInfo);
    if (!err.IsNone()) {
        LOG_ERR() << "Can't get node info: error=" << err.Message();

        return false;
    }

    *outgoingMsg.mutable_node_info() = common::pbconvert::ConvertToProto(*nodeInfo);

    LOG_DBG() << "Send node info: status=" << nodeInfo->mStatus;

    bool isOK = mStream->Write(outgoingMsg);
    if (!isOK) {
        LOG_WRN() << "Stream closed before sending node info";
    }

    return isOK;
}

bool IAMClient::ProcessStartProvisioning(const iamanager::v5::StartProvisioningRequest& request)
{
    LOG_DBG() << "Process start provisioning request";

    iamanager::v5::IAMOutgoingMessages outgoingMsg;
    auto&                              response = *outgoingMsg.mutable_start_provisioning_response();

    auto err = CheckCurrentNodeStatus({NodeStatusEnum::eUnprovisioned});
    if (!err.IsNone()) {
        LOG_ERR() << "Can't start provisioning: wrong node status";

        common::pbconvert::SetErrorInfo(err, response);

        return mStream->Write(outgoingMsg);
    }

    err = mProvisionManager->StartProvisioning(request.password().c_str());
    common::pbconvert::SetErrorInfo(err, response);

    return mStream->Write(outgoingMsg);
}

bool IAMClient::ProcessFinishProvisioning(const iamanager::v5::FinishProvisioningRequest& request)
{
    LOG_DBG() << "Process finish provisioning request";

    iamanager::v5::IAMOutgoingMessages outgoingMsg;
    auto&                              response = *outgoingMsg.mutable_finish_provisioning_response();

    auto err = CheckCurrentNodeStatus({NodeStatusEnum::eUnprovisioned});
    if (!err.IsNone()) {
        LOG_ERR() << "Can't finish provisioning: wrong node status";

        common::pbconvert::SetErrorInfo(err, response);

        return mStream->Write(outgoingMsg);
    }

    err = mProvisionManager->FinishProvisioning(request.password().c_str());
    if (!err.IsNone()) {
        common::pbconvert::SetErrorInfo(err, response);

        return mStream->Write(outgoingMsg);
    }

    err = mNodeInfoProvider->SetNodeStatus(NodeStatusEnum::eProvisioned);
    if (!err.IsNone()) {
        common::pbconvert::SetErrorInfo(err, response);

        return mStream->Write(outgoingMsg);
    }

    common::pbconvert::SetErrorInfo(err, response);

    return mStream->Write(outgoingMsg);
}

bool IAMClient::ProcessDeprovision(const iamanager::v5::DeprovisionRequest& request)
{
    LOG_DBG() << "Process deprovision request";

    iamanager::v5::IAMOutgoingMessages outgoingMsg;
    auto&                              response = *outgoingMsg.mutable_deprovision_response();

    auto err = CheckCurrentNodeStatus({NodeStatusEnum::eProvisioned, NodeStatusEnum::ePaused});
    if (!err.IsNone()) {
        LOG_ERR() << "Can't deprovision: wrong node status";

        common::pbconvert::SetErrorInfo(err, response);

        return mStream->Write(outgoingMsg);
    }

    err = mProvisionManager->Deprovision(request.password().c_str());
    if (!err.IsNone()) {
        common::pbconvert::SetErrorInfo(err, response);

        return mStream->Write(outgoingMsg);
    }

    err = mNodeInfoProvider->SetNodeStatus(NodeStatusEnum::eUnprovisioned);
    if (!err.IsNone()) {
        common::pbconvert::SetErrorInfo(err, response);

        return mStream->Write(outgoingMsg);
    }

    common::pbconvert::SetErrorInfo(err, response);

    return mStream->Write(outgoingMsg);
}

bool IAMClient::ProcessPauseNode(const iamanager::v5::PauseNodeRequest& request)
{
    LOG_DBG() << "Process pause node request";

    (void)request;

    iamanager::v5::IAMOutgoingMessages outgoingMsg;
    auto&                              response = *outgoingMsg.mutable_pause_node_response();

    auto err = CheckCurrentNodeStatus({NodeStatusEnum::eProvisioned});
    if (!err.IsNone()) {
        LOG_ERR() << "Can't pause node: wrong node status";

        common::pbconvert::SetErrorInfo(err, response);

        return mStream->Write(outgoingMsg);
    }

    err = mNodeInfoProvider->SetNodeStatus(NodeStatusEnum::ePaused);
    if (!err.IsNone()) {
        common::pbconvert::SetErrorInfo(err, response);

        return mStream->Write(outgoingMsg);
    }

    common::pbconvert::SetErrorInfo(err, response);

    return SendNodeInfo() && mStream->Write(outgoingMsg);
}

bool IAMClient::ProcessResumeNode(const iamanager::v5::ResumeNodeRequest& request)
{
    LOG_DBG() << "Process resume node request";

    (void)request;

    iamanager::v5::IAMOutgoingMessages outgoingMsg;
    auto&                              response = *outgoingMsg.mutable_resume_node_response();

    auto err = CheckCurrentNodeStatus({NodeStatusEnum::ePaused});
    if (!err.IsNone()) {
        LOG_ERR() << "Can't resume node: wrong node status";

        common::pbconvert::SetErrorInfo(err, response);

        return mStream->Write(outgoingMsg);
    }

    err = mNodeInfoProvider->SetNodeStatus(NodeStatusEnum::eProvisioned);
    if (!err.IsNone()) {
        common::pbconvert::SetErrorInfo(err, response);

        return mStream->Write(outgoingMsg);
    }

    common::pbconvert::SetErrorInfo(err, response);

    return SendNodeInfo() && mStream->Write(outgoingMsg);
}

bool IAMClient::ProcessCreateKey(const iamanager::v5::CreateKeyRequest& request)
{
    const String               nodeID   = request.node_id().c_str();
    const String               certType = request.type().c_str();
    StaticString<cSystemIDLen> subject  = request.subject().c_str();
    const String               password = request.password().c_str();

    LOG_DBG() << "Process create key request: type=" << certType << ", subject=" << subject;

    if (subject.IsEmpty() && !mIdentHandler) {
        LOG_ERR() << "Subject can't be empty";

        return SendCreateKeyResponse(nodeID, certType, {}, AOS_ERROR_WRAP(ErrorEnum::eInvalidArgument));
    }

    Error err = ErrorEnum::eNone;

    if (subject.IsEmpty() && mIdentHandler) {
        Tie(subject, err) = mIdentHandler->GetSystemID();
        if (!err.IsNone()) {
            LOG_ERR() << "Getting system ID error: error=" << AOS_ERROR_WRAP(err);

            return SendCreateKeyResponse(nodeID, certType, {}, AOS_ERROR_WRAP(err));
        }
    }

    auto csr = std::make_unique<StaticString<crypto::cCSRPEMLen>>();

    err = AOS_ERROR_WRAP(mProvisionManager->CreateKey(certType, subject, password, *csr));

    return SendCreateKeyResponse(nodeID, certType, *csr, err);
}

bool IAMClient::ProcessApplyCert(const iamanager::v5::ApplyCertRequest& request)
{
    const String nodeID   = request.node_id().c_str();
    const String certType = request.type().c_str();
    const String pemCert  = request.cert().c_str();

    LOG_DBG() << "Process apply cert request: type=" << certType;

    iam::certhandler::CertInfo certInfo;
    Error                      err = AOS_ERROR_WRAP(mProvisionManager->ApplyCert(certType, pemCert, certInfo));

    return SendApplyCertResponse(nodeID, certType, certInfo.mCertURL, certInfo.mSerial, err);
}

bool IAMClient::ProcessGetCertTypes(const iamanager::v5::GetCertTypesRequest& request)
{
    const String nodeID = request.node_id().c_str();

    LOG_DBG() << "Process get cert types: nodeID=" << nodeID;

    auto [certTypes, err] = mProvisionManager->GetCertTypes();
    if (!err.IsNone()) {
        LOG_ERR() << "Get certificate types failed: error=" << AOS_ERROR_WRAP(err);
    }

    return SendGetCertTypesResponse(certTypes, err);
}

Error IAMClient::CheckCurrentNodeStatus(const std::initializer_list<NodeStatus>& allowedStatuses)
{
    auto nodeInfo = std::make_unique<NodeInfo>();

    auto err = mNodeInfoProvider->GetNodeInfo(*nodeInfo);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    const bool isAllowed = std::any_of(allowedStatuses.begin(), allowedStatuses.end(),
        [currentStatus = nodeInfo->mStatus](const NodeStatus status) { return currentStatus == status; });

    return !isAllowed ? AOS_ERROR_WRAP(ErrorEnum::eWrongState) : ErrorEnum::eNone;
}

bool IAMClient::SendCreateKeyResponse(const String& nodeID, const String& type, const String& csr, const Error& error)
{
    iamanager::v5::IAMOutgoingMessages outgoingMsg;
    auto&                              response = *outgoingMsg.mutable_create_key_response();

    response.set_node_id(nodeID.CStr());
    response.set_type(type.CStr());
    response.set_csr(csr.CStr());

    common::pbconvert::SetErrorInfo(error, response);

    return mStream->Write(outgoingMsg);
}

bool IAMClient::SendApplyCertResponse(
    const String& nodeID, const String& type, const String& certURL, const Array<uint8_t>& serial, const Error& error)
{
    iamanager::v5::IAMOutgoingMessages outgoingMsg;
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
    response.set_type(type.CStr());
    response.set_cert_url(certURL.CStr());
    response.set_serial(protoSerial);

    common::pbconvert::SetErrorInfo(error, response);

    return mStream->Write(outgoingMsg);
}

bool IAMClient::SendGetCertTypesResponse(const iam::provisionmanager::CertTypes& types, const Error& error)
{
    (void)error;

    iamanager::v5::IAMOutgoingMessages outgoingMsg;
    auto&                              response = *outgoingMsg.mutable_cert_types_response();

    for (const auto& type : types) {
        response.mutable_types()->Add(type.CStr());
    }

    return mStream->Write(outgoingMsg);
}

} // namespace aos::iam::iamclient
