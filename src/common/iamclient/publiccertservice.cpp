/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <grpcpp/grpcpp.h>

#include <core/common/tools/logger.hpp>

#include <common/pbconvert/iam.hpp>
#include <common/utils/exception.hpp>

#include "publiccertservice.hpp"

namespace aos::common::iamclient {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

PublicCertService::~PublicCertService()
{
    for (auto& [certType, manager] : mSubscriptions) {
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

    auto [credentials, err] = mTLSCredentials->GetTLSClientCredentials(mInsecureConnection);
    if (!err.IsNone()) {
        return err;
    }

    mCredentials = credentials;

    mStub = iamanager::v6::IAMPublicCertService::NewStub(
        grpc::CreateCustomChannel(mIAMPublicServerURL, mCredentials, grpc::ChannelArguments()));

    return ErrorEnum::eNone;
}

Error PublicCertService::Reconnect()
{
    std::lock_guard lock {mMutex};

    LOG_INF() << "Reconnect public cert service";

    auto [credentials, err] = mTLSCredentials->GetTLSClientCredentials(mInsecureConnection);
    if (!err.IsNone()) {
        return err;
    }

    mCredentials = credentials;

    mStub = iamanager::v6::IAMPublicCertService::NewStub(
        grpc::CreateCustomChannel(mIAMPublicServerURL, mCredentials, grpc::ChannelArguments()));

    for (auto& [certType, manager] : mSubscriptions) {
        if (manager) {
            manager->Reconnect(mStub.get());
        }
    }

    return ErrorEnum::eNone;
}

Error PublicCertService::SubscribeListener(const String& certType, aos::iamclient::CertListenerItf& certListener)
{
    std::lock_guard lock {mMutex};

    LOG_INF() << "Subscribe to certificate changed" << Log::Field("certType", certType);

    auto& manager = mSubscriptions[certType.CStr()];
    if (!manager) {
        iamanager::v6::SubscribeCertChangedRequest request;
        request.set_type(certType.CStr());

        auto convertFunc = [](const iamanager::v6::CertInfo& proto, CertInfo& aos) -> Error {
            return pbconvert::ConvertToAos(proto, aos);
        };

        auto notifyFunc = [](aos::iamclient::CertListenerItf& listener, const CertInfo& certInfo) {
            listener.OnCertChanged(certInfo);
        };

        manager = std::make_unique<CertSubscriptionManager>(mStub.get(), request,
            &iamanager::v6::IAMPublicCertService::Stub::SubscribeCertChanged, convertFunc, notifyFunc,
            std::string("CertSubscription:") + certType.CStr());
    }

    return manager->Subscribe(certListener);
}

Error PublicCertService::UnsubscribeListener(aos::iamclient::CertListenerItf& certListener)
{
    std::lock_guard lock {mMutex};

    LOG_INF() << "Unsubscribe from certificate changed";

    for (auto it = mSubscriptions.begin(); it != mSubscriptions.end();) {
        auto& manager = it->second;

        if (manager->Unsubscribe(certListener)) {
            LOG_INF() << "Unsubscribe from certificate changed: certType=" << it->first.c_str();
            it = mSubscriptions.erase(it);
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

    LOG_INF() << "Get certificate" << Log::Field("certType", certType);

    auto ctx = std::make_unique<grpc::ClientContext>();
    ctx->set_deadline(std::chrono::system_clock::now() + cServiceTimeout);

    iamanager::v6::GetCertRequest request;
    iamanager::v6::CertInfo       certInfoResponse;

    request.set_type(certType.CStr());

    aos::StaticString<aos::crypto::cSerialNumStrLen> serialStr;

    auto err = serialStr.ByteArrayToHex(serial);
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    request.set_issuer(issuer.Get(), issuer.Size());
    request.set_serial(serialStr.CStr());

    if (auto status = mStub->GetCert(ctx.get(), request, &certInfoResponse); !status.ok()) {
        return Error(ErrorEnum::eRuntime, status.error_message().c_str());
    }

    resCert.mCertURL = certInfoResponse.cert_url().c_str();
    resCert.mKeyURL  = certInfoResponse.key_url().c_str();

    LOG_DBG() << "Certificate received" << Log::Field("certURL", resCert.mCertURL)
              << Log::Field("keyURL", resCert.mKeyURL);

    return ErrorEnum::eNone;
}

} // namespace aos::common::iamclient
