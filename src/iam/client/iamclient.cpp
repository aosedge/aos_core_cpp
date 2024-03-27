/*
 * Copyright (C) 2024 Renesas Electronics Corporation.
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "iamclient.hpp"
#include "grpchelper.hpp"
#include "iam/log.hpp"

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

aos::Error IAMClient::Init(const Config& config, aos::iam::certhandler::CertHandlerItf& certHandler,
    aos::cryptoutils::CertLoaderItf& certLoader, aos::crypto::x509::ProviderItf& cryptoProvider, bool provisioningMode)
{

    if (provisioningMode) {
        mCredetials = grpc::InsecureChannelCredentials();
    } else {
        aos::iam::certhandler::CertInfo certInfo;

        auto err = certHandler.GetCertificate(aos::String(config.mCertStorage.c_str()), {}, {}, certInfo);
        if (!err.IsNone()) {
            LOG_ERR() << "Get certificates failed, error = " << err.Message();

            return AOS_ERROR_WRAP(aos::ErrorEnum::eInvalidArgument);
        }

        mCredetials = GetTlsChannelCredentials(certInfo, certLoader, cryptoProvider);
    }

    for (const auto& iamCfg : config.mRemoteIAMs) {
        mRemoteIMs[iamCfg.mNodeID.c_str()] = {iamCfg, nullptr};
    }

    if (mRemoteIMs.size() > cMaxNodes) {
        mRemoteIMs.clear();

        return AOS_ERROR_WRAP(aos::ErrorEnum::eNoMemory);
    }

    return aos::ErrorEnum::eNone;
}

aos::Array<aos::StaticString<aos::cNodeIDLen>> IAMClient::GetRemoteNodes()
{
    aos::StaticArray<aos::StaticString<aos::cNodeIDLen>, cMaxNodes> result;

    std::lock_guard lock(mMutex);

    for (const auto& pair : mRemoteIMs) {
        result.PushBack(pair.first.c_str());
    }

    return result;
}

aos::Error IAMClient::GetCertTypes(
    const aos::String& nodeID, aos::Array<aos::StaticString<aos::iam::certhandler::cCertTypeLen>>& certTypes)
{
    auto stub = CreateIAMProvisioningServiceStub(nodeID);
    if (!stub) {
        return AOS_ERROR_WRAP(aos::ErrorEnum::eFailed);
    }

    iamanager::v4::GetCertTypesRequest request;
    request.set_node_id(nodeID.CStr());

    iamanager::v4::CertTypes response;

    grpc::ClientContext ctx;
    SetClientContext(ctx, nodeID);

    if (const auto status = stub->GetCertTypes(&ctx, request, &response); !status.ok()) {
        LOG_DBG() << "GetCertTypes failed. error_code:" << status.error_code()
                  << "error_message = " << status.error_message().c_str();

        return AOS_ERROR_WRAP(aos::ErrorEnum::eFailed);
    }

    for (const auto& certType : response.types()) {
        if (auto err = certTypes.PushBack(certType.c_str()); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    return aos::ErrorEnum::eNone;
}

aos::Error IAMClient::SetOwner(const aos::String& nodeID, const aos::String& certType, const aos::String& password)
{
    auto stub = CreateIAMProvisioningServiceStub(nodeID);
    if (!stub) {
        return AOS_ERROR_WRAP(aos::ErrorEnum::eFailed);
    }

    iamanager::v4::SetOwnerRequest request;
    request.set_node_id(nodeID.CStr());
    request.set_type(certType.CStr());
    request.set_password(password.CStr());

    google::protobuf::Empty response;

    grpc::ClientContext ctx;
    SetClientContext(ctx, nodeID);

    if (const auto status = stub->SetOwner(&ctx, request, &response); !status.ok()) {
        LOG_DBG() << "SetOwner failed. error_message = " << status.error_message().c_str();

        return AOS_ERROR_WRAP(aos::ErrorEnum::eFailed);
    }

    return aos::ErrorEnum::eNone;
}

aos::Error IAMClient::Clear(const aos::String& nodeID, const aos::String& certType)
{
    auto stub = CreateIAMProvisioningServiceStub(nodeID);
    if (!stub) {
        return AOS_ERROR_WRAP(aos::ErrorEnum::eFailed);
    }

    iamanager::v4::ClearRequest request;
    request.set_node_id(nodeID.CStr());
    request.set_type(certType.CStr());

    google::protobuf::Empty response;

    grpc::ClientContext ctx;
    SetClientContext(ctx, nodeID);

    if (const auto status = stub->Clear(&ctx, request, &response); !status.ok()) {
        LOG_DBG() << "Clear failed. error_message = " << status.error_message().c_str();

        return AOS_ERROR_WRAP(aos::ErrorEnum::eFailed);
    }

    return aos::ErrorEnum::eNone;
}

aos::Error IAMClient::CreateKey(const aos::String& nodeID, const aos::String& certType,
    const aos::String& subjectCommonName, const aos::String& password, aos::String& pemCSR)
{
    auto stub = CreateIAMCertificateServiceStub(nodeID);
    if (!stub) {
        return AOS_ERROR_WRAP(aos::ErrorEnum::eFailed);
    }

    iamanager::v4::CreateKeyRequest request;
    request.set_node_id(nodeID.CStr());
    request.set_type(certType.CStr());
    request.set_subject(subjectCommonName.CStr());
    request.set_password(password.CStr());

    iamanager::v4::CreateKeyResponse response;

    grpc::ClientContext ctx;
    SetClientContext(ctx, nodeID);

    if (const auto status = stub->CreateKey(&ctx, request, &response); !status.ok()) {
        LOG_DBG() << "CreateKey failed. error_message = " << status.error_message().c_str();

        return AOS_ERROR_WRAP(aos::ErrorEnum::eFailed);
    }

    pemCSR = response.csr().c_str();

    return aos::ErrorEnum::eNone;
}

aos::Error IAMClient::ApplyCertificate(const aos::String& nodeID, const aos::String& certType,
    const aos::String& pemCert, aos::iam::certhandler::CertInfo& info)
{
    auto stub = CreateIAMCertificateServiceStub(nodeID);
    if (!stub) {
        return AOS_ERROR_WRAP(aos::ErrorEnum::eFailed);
    }

    iamanager::v4::ApplyCertRequest request;
    request.set_node_id(nodeID.CStr());
    request.set_type(certType.CStr());
    request.set_cert(pemCert.CStr());

    iamanager::v4::ApplyCertResponse response;

    grpc::ClientContext ctx;
    SetClientContext(ctx, nodeID);

    if (const auto status = stub->ApplyCert(&ctx, request, &response); !status.ok()) {
        LOG_DBG() << "ApplyCert failed. error_message = " << status.error_message().c_str();

        return AOS_ERROR_WRAP(aos::ErrorEnum::eFailed);
    }

    info.mCertURL = response.cert_url().c_str();

    for (auto ch : response.serial()) {
        if (auto err = info.mSerial.PushBack(ch); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    return aos::ErrorEnum::eNone;
}

aos::Error IAMClient::EncryptDisk(const aos::String& nodeID, const aos::String& password)
{
    auto stub = CreateIAMProvisioningServiceStub(nodeID);
    if (!stub) {
        return AOS_ERROR_WRAP(aos::ErrorEnum::eFailed);
    }

    iamanager::v4::EncryptDiskRequest request;
    request.set_node_id(nodeID.CStr());
    request.set_password(password.CStr());

    google::protobuf::Empty response;

    grpc::ClientContext ctx;
    SetClientContext(ctx, nodeID);

    if (const auto status = stub->EncryptDisk(&ctx, request, &response); !status.ok()) {
        LOG_DBG() << "EncryptDisk failed. error_message = " << status.error_message().c_str();

        return AOS_ERROR_WRAP(aos::ErrorEnum::eFailed);
    }

    return aos::ErrorEnum::eNone;
}

aos::Error IAMClient::FinishProvisioning(const aos::String& nodeID)
{
    auto stub = CreateIAMProvisioningServiceStub(nodeID);
    if (!stub) {
        return AOS_ERROR_WRAP(aos::ErrorEnum::eFailed);
    }

    google::protobuf::Empty request, response;

    grpc::ClientContext ctx;
    SetClientContext(ctx, nodeID);

    if (const auto status = stub->FinishProvisioning(&ctx, request, &response); !status.ok()) {
        LOG_DBG() << "FinishProvisioning failed. error_message = " << status.error_message().c_str();

        return AOS_ERROR_WRAP(aos::ErrorEnum::eFailed);
    }

    return aos::ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * Protected
 **********************************************************************************************************************/

CertificateServiceStubPtr IAMClient::CreateIAMCertificateServiceStub(const aos::String& nodeId)
{
    std::lock_guard lock(mMutex);

    if (const auto it = mRemoteIMs.find(nodeId.CStr()); it != mRemoteIMs.cend()) {
        auto& remoteIM = it->second;

        if (!remoteIM.mChannel) {
            grpc::ChannelArguments channelArgs;

            channelArgs.SetSslTargetNameOverride("Aos Core");

            remoteIM.mChannel = grpc::CreateCustomChannel(remoteIM.mRemoteIAMConfig.mURL, mCredetials, channelArgs);
        }

        return CertificateService::NewStub(remoteIM.mChannel);
    }

    return nullptr;
}

ProvisioningServiceStubPtr IAMClient::CreateIAMProvisioningServiceStub(const aos::String& nodeId)
{
    std::lock_guard lock(mMutex);

    if (const auto it = mRemoteIMs.find(nodeId.CStr()); it != mRemoteIMs.cend()) {
        auto& remoteIM = it->second;

        if (!remoteIM.mChannel) {
            grpc::ChannelArguments channelArgs;

            channelArgs.SetSslTargetNameOverride("Aos Core");

            remoteIM.mChannel = grpc::CreateCustomChannel(remoteIM.mRemoteIAMConfig.mURL, mCredetials, channelArgs);
        }

        return ProvisioningService::NewStub(remoteIM.mChannel);
    }

    return nullptr;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

void IAMClient::SetClientContext(grpc::ClientContext& context, const aos::String& nodeId)
{
    std::lock_guard lock(mMutex);

    if (const auto it = mRemoteIMs.find(nodeId.CStr()); it != mRemoteIMs.cend()) {
        context.set_deadline(std::chrono::system_clock::now() + it->second.mRemoteIAMConfig.mRequestTimeout);
    }
}
