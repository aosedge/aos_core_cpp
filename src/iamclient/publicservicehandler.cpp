/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "iamclient/publicservicehandler.hpp"
#include "logger/logmodule.hpp"
#include "utils/grpchelper.hpp"

namespace aos::common::iamclient {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error PublicServiceHandler::Init(const Config& cfg, crypto::CertLoaderItf& certLoader,
    crypto::x509::ProviderItf& cryptoProvider, bool insecureConnection, MTLSCredentialsFunc mtlsCredentialsFunc)
{
    LOG_INF() << "Initializing public service handler: insecureConnection=" << insecureConnection;

    mConfig              = cfg;
    mCertLoader          = &certLoader;
    mCryptoProvider      = &cryptoProvider;
    mMTLSCredentialsFunc = std::move(mtlsCredentialsFunc);

    if (auto err = CreateCredentials(insecureConnection); !err.IsNone()) {
        return err;
    }

    mStub = iamanager::v5::IAMPublicService::NewStub(
        grpc::CreateCustomChannel(mConfig.mIAMPublicServerURL, mCredentials, grpc::ChannelArguments()));

    return ErrorEnum::eNone;
}

RetWithError<std::shared_ptr<grpc::ChannelCredentials>> PublicServiceHandler::GetMTLSConfig(
    const std::string& certStorage)
{
    iam::certhandler::CertInfo certInfo;

    LOG_DBG() << "Getting MTLS config: certStorage=" << certStorage.c_str();

    if (auto err = GetCertificate(certStorage, certInfo); !err.IsNone()) {
        return {nullptr, err};
    }

    return {mMTLSCredentialsFunc(certInfo, mConfig.mCACert.c_str(), *mCertLoader, *mCryptoProvider), ErrorEnum::eNone};
}

std::shared_ptr<grpc::ChannelCredentials> PublicServiceHandler::GetTLSCredentials()
{
    if (!mConfig.mCACert.empty()) {
        LOG_DBG() << "Getting TLS config";

        return common::utils::GetTLSClientCredentials(mConfig.mCACert.c_str());
    }

    return nullptr;
}

Error PublicServiceHandler::GetCertificate(const std::string& certType, iam::certhandler::CertInfo& certInfo)
{
    auto ctx = std::make_unique<grpc::ClientContext>();
    ctx->set_deadline(std::chrono::system_clock::now() + cIAMPublicServiceTimeout);

    iamanager::v5::GetCertRequest request;
    iamanager::v5::CertInfo       certInfoResponse;

    request.set_type(certType);

    if (auto status = mStub->GetCert(ctx.get(), request, &certInfoResponse); !status.ok()) {
        return Error(ErrorEnum::eRuntime, status.error_message().c_str());
    }

    certInfo.mCertURL = certInfoResponse.cert_url().c_str();
    certInfo.mKeyURL  = certInfoResponse.key_url().c_str();

    LOG_DBG() << "Certificate received: certURL=" << certInfo.mCertURL.CStr() << ", keyURL=" << certInfo.mKeyURL.CStr();

    return ErrorEnum::eNone;
}

void PublicServiceHandler::Close()
{
    std::unique_lock lock {mMutex};

    LOG_INF() << "Closing public service handler";

    for (auto& [certType, subscription] : mSubscriptions) {
        (void)certType;

        subscription.mClose = true;
        if (subscription.mCtx) {
            subscription.mCtx->TryCancel();
        }

        subscription.mSubscribers.clear();

        lock.unlock();

        mCV.notify_all();

        if (subscription.mFuture.valid()) {
            subscription.mFuture.wait();
        }

        lock.lock();
    }
}

Error PublicServiceHandler::SubscribeCertChanged(
    const std::string& certType, iam::certhandler::CertReceiverItf& subscriber)
{
    std::lock_guard lock {mMutex};

    LOG_INF() << "Subscribing to certificate changed: certType=" << certType.c_str();

    auto& subscription = mSubscriptions[certType];

    if (!subscription.mSubscribers.insert(&subscriber).second) {
        return Error(ErrorEnum::eAlreadyExist, "Subscriber already exists for this cert type");
    }

    if (subscription.mSubscribers.size() == 1) {
        subscription.mFuture
            = std::async(std::launch::async, &PublicServiceHandler::RunTask, this, certType, &subscription);
    }

    return ErrorEnum::eNone;
}

void PublicServiceHandler::UnsubscribeCertChanged(
    const std::string& certType, iam::certhandler::CertReceiverItf& subscriber)
{
    decltype(mSubscriptions)::iterator it;

    {
        std::lock_guard lock {mMutex};

        LOG_INF() << "Unsubscribing from certificate changed: certType=" << certType.c_str();

        if (it = mSubscriptions.find(certType); it == mSubscriptions.end()) {
            LOG_WRN() << "Subscription not found: certType=" << certType.c_str();

            return;
        }

        auto& subscription = it->second;

        if (subscription.mSubscribers.erase(&subscriber) == 0) {
            LOG_WRN() << "Subscriber not found for certType=" << certType.c_str();

            return;
        }

        if (!subscription.mSubscribers.empty()) {
            return;
        }

        subscription.mClose = true;

        if (subscription.mCtx) {
            subscription.mCtx->TryCancel();
        }
    }

    mCV.notify_all();

    if (it->second.mFuture.valid()) {
        it->second.mFuture.wait();
    }

    {
        std::lock_guard lock {mMutex};

        mSubscriptions.erase(it);
    }
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

Error PublicServiceHandler::CreateCredentials(bool insecureConnection)
{
    try {
        if (insecureConnection) {
            mCredentials = grpc::InsecureChannelCredentials();

            return ErrorEnum::eNone;
        }

        mCredentials = common::utils::GetTLSClientCredentials(mConfig.mCACert.c_str());
    } catch (const std::exception& e) {
        return Error(ErrorEnum::eRuntime, e.what());
    }

    return ErrorEnum::eNone;
}

void PublicServiceHandler::RunTask(const std::string& certType, Subscription* subscription)
{
    LOG_DBG() << "Subscription task started: certType=" << certType.c_str();

    while (true) {
        try {
            {
                std::lock_guard lock {mMutex};

                if (subscription->mClose) {
                    break;
                }
            }

            auto                                       ctx = std::make_unique<grpc::ClientContext>();
            iamanager::v5::SubscribeCertChangedRequest request;

            request.set_type(certType);

            std::unique_ptr<grpc::ClientReader<iamanager::v5::CertInfo>> reader(
                mStub->SubscribeCertChanged(ctx.get(), request));

            {
                std::lock_guard lock {mMutex};

                subscription->mCtx = std::move(ctx);
            }

            iamanager::v5::CertInfo certInfo;

            while (reader->Read(&certInfo)) {
                LOG_DBG() << "Certificate changed: certURL=" << certInfo.cert_url().c_str()
                          << ", keyURL=" << certInfo.key_url().c_str();

                {
                    std::lock_guard lock {mMutex};

                    for (auto subscriber : subscription->mSubscribers) {
                        iam::certhandler::CertInfo iamCertInfo;

                        iamCertInfo.mCertURL = certInfo.cert_url().c_str();
                        iamCertInfo.mKeyURL  = certInfo.key_url().c_str();

                        subscriber->OnCertChanged(iamCertInfo);
                    }
                }
            }
        } catch (const std::exception& e) {
            LOG_ERR() << "Subscription loop failed: error=" << e.what();
        }

        {
            std::unique_lock lock {mMutex};

            mCV.wait_for(lock, cReconnectInterval, [this, subscription]() { return subscription->mClose; });
        }
    }

    LOG_DBG() << "Subscription task stopped: certType=" << certType.c_str();
}

} // namespace aos::common::iamclient
