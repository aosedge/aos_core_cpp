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
        mCredentials = grpc::InsecureChannelCredentials();
    } else {
        aos::iam::certhandler::CertInfo certInfo;

        auto err = certHandler.GetCertificate(aos::String(config.mCertStorage.c_str()), {}, {}, certInfo);
        if (!err.IsNone()) {
            LOG_ERR() << "Get certificates failed, error = " << err.Message();

            return AOS_ERROR_WRAP(aos::ErrorEnum::eInvalidArgument);
        }

        mCredentials = GetTlsChannelCredentials(certInfo, config.mCACert.c_str(), certLoader, cryptoProvider);
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
    auto stub = CreateIAMProvisioningServiceStub(nodeID.CStr());
    if (!stub) {
        return AOS_ERROR_WRAP(aos::ErrorEnum::eFailed);
    }

    iamanager::v4::GetCertTypesRequest request;
    request.set_node_id(nodeID.CStr());

    iamanager::v4::CertTypes response;
    auto                     ctx = GetClientContext(nodeID.CStr(), cDefaultRequestTimeout);

    if (const auto status = stub->GetCertTypes(ctx.get(), request, &response); !status.ok()) {
        LOG_ERR() << "Get cert types failed: code = " << status.error_code()
                  << ", message = " << status.error_message().c_str();

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
    auto stub = CreateIAMProvisioningServiceStub(nodeID.CStr());
    if (!stub) {
        return AOS_ERROR_WRAP(aos::ErrorEnum::eFailed);
    }

    iamanager::v4::SetOwnerRequest request;
    request.set_node_id(nodeID.CStr());
    request.set_type(certType.CStr());
    request.set_password(password.CStr());

    google::protobuf::Empty response;
    auto                    ctx = GetClientContext(nodeID.CStr(), cDefaultRequestTimeout);

    if (const auto status = stub->SetOwner(ctx.get(), request, &response); !status.ok()) {
        LOG_ERR() << "Get owner failed: code = " << status.error_code()
                  << ", message = " << status.error_message().c_str();

        return AOS_ERROR_WRAP(aos::ErrorEnum::eFailed);
    }

    return aos::ErrorEnum::eNone;
}

aos::Error IAMClient::Clear(const aos::String& nodeID, const aos::String& certType)
{
    auto stub = CreateIAMProvisioningServiceStub(nodeID.CStr());
    if (!stub) {
        return AOS_ERROR_WRAP(aos::ErrorEnum::eFailed);
    }

    iamanager::v4::ClearRequest request;
    request.set_node_id(nodeID.CStr());
    request.set_type(certType.CStr());

    google::protobuf::Empty response;
    auto                    ctx = GetClientContext(nodeID.CStr(), cDefaultRequestTimeout);

    if (const auto status = stub->Clear(ctx.get(), request, &response); !status.ok()) {
        LOG_ERR() << "Clear failed: code = " << status.error_code() << ", message = " << status.error_message().c_str();

        return AOS_ERROR_WRAP(aos::ErrorEnum::eFailed);
    }

    return aos::ErrorEnum::eNone;
}

aos::Error IAMClient::CreateKey(const aos::String& nodeID, const aos::String& certType,
    const aos::String& subjectCommonName, const aos::String& password, aos::String& pemCSR)
{
    auto stub = CreateIAMCertificateServiceStub(nodeID.CStr());
    if (!stub) {
        return AOS_ERROR_WRAP(aos::ErrorEnum::eFailed);
    }

    iamanager::v4::CreateKeyRequest request;
    request.set_node_id(nodeID.CStr());
    request.set_type(certType.CStr());
    request.set_subject(subjectCommonName.CStr());
    request.set_password(password.CStr());

    iamanager::v4::CreateKeyResponse response;
    auto                             ctx = GetClientContext(nodeID.CStr(), cDefaultRequestTimeout);

    if (const auto status = stub->CreateKey(ctx.get(), request, &response); !status.ok()) {
        LOG_ERR() << "Create key failed: code = " << status.error_code()
                  << ", message = " << status.error_message().c_str();

        return AOS_ERROR_WRAP(aos::ErrorEnum::eFailed);
    }

    pemCSR = response.csr().c_str();

    return aos::ErrorEnum::eNone;
}

aos::Error IAMClient::ApplyCertificate(const aos::String& nodeID, const aos::String& certType,
    const aos::String& pemCert, aos::iam::certhandler::CertInfo& info)
{
    auto stub = CreateIAMCertificateServiceStub(nodeID.CStr());
    if (!stub) {
        return AOS_ERROR_WRAP(aos::ErrorEnum::eFailed);
    }

    iamanager::v4::ApplyCertRequest request;
    request.set_node_id(nodeID.CStr());
    request.set_type(certType.CStr());
    request.set_cert(pemCert.CStr());

    iamanager::v4::ApplyCertResponse response;
    auto                             ctx = GetClientContext(nodeID.CStr(), cDefaultRequestTimeout);

    if (const auto status = stub->ApplyCert(ctx.get(), request, &response); !status.ok()) {
        LOG_ERR() << "Apply certificate failed: code = " << status.error_code()
                  << ", message = " << status.error_message().c_str();

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
    auto stub = CreateIAMProvisioningServiceStub(nodeID.CStr());
    if (!stub) {
        return AOS_ERROR_WRAP(aos::ErrorEnum::eFailed);
    }

    iamanager::v4::EncryptDiskRequest request;
    request.set_node_id(nodeID.CStr());
    request.set_password(password.CStr());

    google::protobuf::Empty response;
    auto                    ctx = GetClientContext(nodeID.CStr(), cDefaultEncryptTimeout);

    if (const auto status = stub->EncryptDisk(ctx.get(), request, &response); !status.ok()) {
        LOG_ERR() << "Disk encryption failed: code = " << status.error_code()
                  << ", message = " << status.error_message().c_str();

        return AOS_ERROR_WRAP(aos::ErrorEnum::eFailed);
    }

    return aos::ErrorEnum::eNone;
}

aos::Error IAMClient::FinishProvisioning(const aos::String& nodeID)
{
    auto stub = CreateIAMProvisioningServiceStub(nodeID.CStr());
    if (!stub) {
        return AOS_ERROR_WRAP(aos::ErrorEnum::eFailed);
    }

    google::protobuf::Empty request, response;
    auto                    ctx = GetClientContext(nodeID.CStr(), cDefaultRequestTimeout);

    if (const auto status = stub->FinishProvisioning(ctx.get(), request, &response); !status.ok()) {
        LOG_ERR() << "Finish provisioning failed: code = " << status.error_code()
                  << ", message = " << status.error_message().c_str();

        return AOS_ERROR_WRAP(aos::ErrorEnum::eFailed);
    }

    return aos::ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * Protected
 **********************************************************************************************************************/

CertificateServiceStubPtr IAMClient::CreateIAMCertificateServiceStub(const std::string& nodeID)
{
    std::lock_guard lock(mMutex);

    if (const auto it = mRemoteIMs.find(nodeID); it != mRemoteIMs.cend()) {
        auto& remoteIM = it->second;

        if (!remoteIM.mChannel) {
            remoteIM.mChannel
                = grpc::CreateCustomChannel(remoteIM.mConfig.mURL, mCredentials, grpc::ChannelArguments());
        }

        return CertificateService::NewStub(remoteIM.mChannel);
    }

    return nullptr;
}

ProvisioningServiceStubPtr IAMClient::CreateIAMProvisioningServiceStub(const std::string& nodeID)
{
    std::lock_guard lock(mMutex);

    if (const auto it = mRemoteIMs.find(nodeID); it != mRemoteIMs.cend()) {
        auto& remoteIM = it->second;

        if (!remoteIM.mChannel) {
            remoteIM.mChannel
                = grpc::CreateCustomChannel(remoteIM.mConfig.mURL, mCredentials, grpc::ChannelArguments());
        }

        return ProvisioningService::NewStub(remoteIM.mChannel);
    }

    return nullptr;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

std::unique_ptr<grpc::ClientContext> IAMClient::GetClientContext(
    const std::string& nodeID, UtilsTime::Duration defaultTimeout)
{
    std::lock_guard lock(mMutex);

    auto                ctx     = std::make_unique<grpc::ClientContext>();
    UtilsTime::Duration timeout = defaultTimeout;

    if (const auto it = mRemoteIMs.find(nodeID); it != mRemoteIMs.cend()) {
        if (it->second.mConfig.mRequestTimeout > UtilsTime::Duration::zero()) {
            timeout = it->second.mConfig.mRequestTimeout;
        }
    }

    ctx->set_deadline(std::chrono::system_clock::now() + timeout);

    return ctx;
}
