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

    if (mInsecureConnection) {
        mCredentials = grpc::InsecureChannelCredentials();
    } else {
        auto [credentials, err] = mTLSCredentials->GetTLSClientCredentials();
        if (!err.IsNone()) {
            return err;
        }

        mCredentials = credentials;
    }

    mStub = iamanager::v7::IAMPublicCertService::NewStub(
        grpc::CreateCustomChannel(mIAMPublicServerURL, mCredentials, common::utils::CreateGRPCChannelArguments()));

    return ErrorEnum::eNone;
}

Error PublicCertService::Reconnect()
{
    std::lock_guard lock {mMutex};

    LOG_INF() << "Reconnect public cert service";

    if (mInsecureConnection) {
        mCredentials = grpc::InsecureChannelCredentials();
    } else {
        auto [credentials, err] = mTLSCredentials->GetTLSClientCredentials();
        if (!err.IsNone()) {
            return err;
        }

        mCredentials = credentials;
    }

    mStub = iamanager::v7::IAMPublicCertService::NewStub(
        grpc::CreateCustomChannel(mIAMPublicServerURL, mCredentials, common::utils::CreateGRPCChannelArguments()));

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

    LOG_DBG() << "Subscribe to certificate changed" << Log::Field("certType", certType);

    auto& manager = mSubscriptions[certType.CStr()];
    if (!manager) {
        iamanager::v7::SubscribeCertsChangedRequest request;
        request.set_type(certType.CStr());

        auto convertFunc = [](const iamanager::v7::CertInfoList& proto, iamanager::v7::CertInfoList& aos) -> Error {
            aos = proto;

            return ErrorEnum::eNone;
        };

        auto notifyFunc = [](aos::iamclient::CertListenerItf& listener, const iamanager::v7::CertInfoList& certList) {
            for (const auto& protoCert : certList.certs()) {
                auto certInfo = std::make_unique<CertInfo>();

                if (auto err = pbconvert::ConvertToAos(protoCert, *certInfo); !err.IsNone()) {
                    LOG_ERR() << "Failed to convert cert info" << Log::Field(err);

                    continue;
                }

                listener.OnCertChanged(*certInfo);
            }
        };

        manager = std::make_unique<CertSubscriptionManager>(mStub.get(), request,
            &iamanager::v7::IAMPublicCertService::Stub::SubscribeCertsChanged, convertFunc, notifyFunc,
            std::string("CertSubscription:") + certType.CStr());
    }

    return manager->Subscribe(certListener);
}

Error PublicCertService::UnsubscribeListener(aos::iamclient::CertListenerItf& certListener)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Unsubscribe from certificate changed";

    for (auto it = mSubscriptions.begin(); it != mSubscriptions.end();) {
        auto& manager = it->second;

        if (manager->Unsubscribe(certListener)) {
            LOG_DBG() << "Unsubscribe from certificate changed" << Log::Field("certType", it->first.c_str());

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

    LOG_DBG() << "Get certificate" << Log::Field("certType", certType);

    auto ctx = std::make_unique<grpc::ClientContext>();
    ctx->set_deadline(std::chrono::system_clock::now() + cServiceTimeout);

    iamanager::v7::GetCertRequest request;
    iamanager::v7::CertInfo       certInfoResponse;

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

Error PublicCertService::GetAllCerts(const String& certType, Array<CertInfo>& resCerts) const
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Get all certificates" << Log::Field("certType", certType);

    auto ctx = std::make_unique<grpc::ClientContext>();
    ctx->set_deadline(std::chrono::system_clock::now() + cServiceTimeout);

    iamanager::v7::GetCertRequest request;
    iamanager::v7::CertInfoList   response;

    request.set_type(certType.CStr());

    if (auto status = mStub->GetAllCerts(ctx.get(), request, &response); !status.ok()) {
        return Error(ErrorEnum::eRuntime, status.error_message().c_str());
    }

    for (const auto& certInfoResponse : response.certs()) {
        auto certInfo = std::make_unique<CertInfo>();

        certInfo->mCertURL = certInfoResponse.cert_url().c_str();
        certInfo->mKeyURL  = certInfoResponse.key_url().c_str();
        certInfo->mIssuer  = Array<uint8_t>(
            reinterpret_cast<const uint8_t*>(certInfoResponse.issuer().data()), certInfoResponse.issuer().size());

        if (auto err = String(certInfoResponse.serial().c_str()).HexToByteArray(certInfo->mSerial); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (auto err = resCerts.PushBack(*certInfo); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    return ErrorEnum::eNone;
}

} // namespace aos::common::iamclient
