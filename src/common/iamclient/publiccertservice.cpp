/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <grpcpp/grpcpp.h>

#include <memory>

#include <core/common/tools/logger.hpp>

#include <common/pbconvert/iam.hpp>
#include <common/utils/exception.hpp>
#include <common/utils/grpchelper.hpp>

#include "publiccertservice.hpp"

namespace aos::common::iamclient {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

PublicCertService::~PublicCertService()
{
    for (auto& [certType, manager] : mCertKeySubscriptions) {
        if (manager) {
            manager->Close();
        }
    }
}

Error PublicCertService::Init(
    const std::string& iamPublicServerURL, TLSCredentialsItf& tlsCredentials, bool insecureConnection)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Init public cert service" << Log::Field("iamPublicServerURL", iamPublicServerURL.c_str())
              << Log::Field("insecureConnection", insecureConnection);

    mTLSCredentials     = &tlsCredentials;
    mIAMPublicServerURL = iamPublicServerURL;
    mInsecureConnection = insecureConnection;

    if (mInsecureConnection) {
        mCredentials = grpc::InsecureChannelCredentials();
    } else {
        auto [credentials, err] = mTLSCredentials->GetTLSClientCredentials();
        if (!err.IsNone()) {
            return err;
        }

        mCredentials = credentials;
    }

    mStub = iamanager::v7::IAMPublicCertificateService::NewStub(
        grpc::CreateCustomChannel(mIAMPublicServerURL, mCredentials, common::utils::CreateGRPCChannelArguments()));

    return ErrorEnum::eNone;
}

Error PublicCertService::Reconnect()
{
    std::lock_guard lock {mMutex};

    LOG_INF() << "Reconnect public cert service";

    auto [credentials, err] = mTLSCredentials->GetTLSClientCredentials();
    if (!err.IsNone()) {
        return err;
    }

    mCredentials = credentials;

    mStub = iamanager::v7::IAMPublicCertificateService::NewStub(
        grpc::CreateCustomChannel(mIAMPublicServerURL, mCredentials, common::utils::CreateGRPCChannelArguments()));

    for (auto& [certType, manager] : mCertKeySubscriptions) {
        if (manager) {
            manager->Reconnect(mStub.get());
        }
    }

    return ErrorEnum::eNone;
}

Error PublicCertService::SubscribeListener(const String& certType, aos::iamclient::CertListenerItf& certListener)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Subscribe to certificate changed" << Log::Field("certType", certType);

    auto& manager = mCertKeySubscriptions[certType.CStr()];
    if (!manager) {
        iamanager::v7::SubscribeCertKeyChangedRequest request;
        request.set_storage_id(certType.CStr());

        auto convertFunc = [](const iamanager::v7::CertKeyInfo& proto, iamanager::v7::CertKeyInfo& aos) -> Error {
            aos = proto;

            return ErrorEnum::eNone;
        };

        auto notifyFunc = [](aos::iamclient::CertListenerItf& listener, const iamanager::v7::CertKeyInfo& protoCert) {
            auto certInfo = std::make_unique<CertInfo>();

            if (auto err = pbconvert::ConvertToAos(protoCert, *certInfo); !err.IsNone()) {
                LOG_ERR() << "Failed to convert cert info" << Log::Field(err);

                return;
            }

            listener.OnCertChanged(*certInfo);
        };

        manager = std::make_unique<CertKeySubscriptionManager>(mStub.get(), request,
            &iamanager::v7::IAMPublicCertificateService::Stub::SubscribeCertKeyChanged, convertFunc, notifyFunc,
            std::string("CertKeySubscription:") + certType.CStr());
    }

    return manager->Subscribe(certListener);
}

Error PublicCertService::UnsubscribeListener(aos::iamclient::CertListenerItf& certListener)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Unsubscribe from certificate changed";

    for (auto it = mCertKeySubscriptions.begin(); it != mCertKeySubscriptions.end();) {
        auto& manager = it->second;

        if (manager->Unsubscribe(certListener)) {
            LOG_DBG() << "Unsubscribe from certificate changed" << Log::Field("certType", it->first.c_str());

            it = mCertKeySubscriptions.erase(it);
        } else {
            ++it;
        }
    }

    return ErrorEnum::eNone;
}

Error PublicCertService::GetCert(
    const String& certType, const Array<uint8_t>& issuer, const Array<uint8_t>& serial, CertInfo& resCert) const
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Get certificate" << Log::Field("certType", certType);

    auto ctx = std::make_unique<grpc::ClientContext>();
    ctx->set_deadline(std::chrono::system_clock::now() + cServiceTimeout);

    iamanager::v7::GetCertKeyRequest request;
    iamanager::v7::CertKeyInfo       response;

    request.set_storage_id(certType.CStr());

    aos::StaticString<aos::crypto::cSerialNumStrLen> serialStr;

    auto err = serialStr.ByteArrayToHex(serial);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    request.set_issuer(issuer.Get(), issuer.Size());
    request.set_serial(serialStr.CStr());

    if (auto status = mStub->GetCertKey(ctx.get(), request, &response); !status.ok()) {
        return Error(ErrorEnum::eRuntime, status.error_message().c_str());
    }

    if (auto err = pbconvert::ConvertToAos(response, resCert); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    LOG_DBG() << "Certificate received" << Log::Field("certURL", resCert.mCertURL)
              << Log::Field("keyURL", resCert.mKeyURL);

    return ErrorEnum::eNone;
}

Error PublicCertService::GetRootCerts(Array<CertInfo>& resCerts) const
{
    (void)resCerts;

    return AOS_ERROR_WRAP(ErrorEnum::eNotSupported);
}

Error PublicCertService::SubscribeRootCertsListener(aos::iamclient::CertListenerItf& certListener)
{
    (void)certListener;

    return AOS_ERROR_WRAP(ErrorEnum::eNotSupported);
}

} // namespace aos::common::iamclient
