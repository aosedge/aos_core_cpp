/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "iamclient/publicservicehandler.hpp"
#include "logger/logmodule.hpp"
#include "pbconvert/common.hpp"
#include "utils/exception.hpp"
#include "utils/grpchelper.hpp"

namespace aos::common::iamclient {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error PublicServiceHandler::Init(const Config& cfg, crypto::CertLoaderItf& certLoader,
    crypto::x509::ProviderItf& cryptoProvider, bool insecureConnection, MTLSCredentialsFunc mtlsCredentialsFunc)
{
    LOG_DBG() << "Init public service handler";

    if (insecureConnection) {
        LOG_WRN() << "Public service: insecure connection is used";
    }

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

RetWithError<std::shared_ptr<grpc::ChannelCredentials>> PublicServiceHandler::GetMTLSClientCredentials(
    const String& certStorage)
{
    iam::certhandler::CertInfo certInfo;

    LOG_DBG() << "Get MTLS config: certStorage=" << certStorage;

    if (auto err = GetCert(certStorage, {}, {}, certInfo); !err.IsNone()) {
        return {nullptr, err};
    }

    return {mMTLSCredentialsFunc(certInfo, mConfig.mCACert.c_str(), *mCertLoader, *mCryptoProvider), ErrorEnum::eNone};
}

RetWithError<std::shared_ptr<grpc::ChannelCredentials>> PublicServiceHandler::GetTLSClientCredentials()
{
    LOG_DBG() << "Get TLS config";

    if (!mConfig.mCACert.empty()) {
        return {common::utils::GetTLSClientCredentials(mConfig.mCACert.c_str()), ErrorEnum::eNone};
    }

    return {nullptr, ErrorEnum::eNone};
}

Error PublicServiceHandler::GetCert(const String& certType, const Array<uint8_t>& issuer, const Array<uint8_t>& serial,
    iam::certhandler::CertInfo& resCert) const
{
    auto ctx = std::make_unique<grpc::ClientContext>();
    ctx->set_deadline(std::chrono::system_clock::now() + cIAMPublicServiceTimeout);

    iamanager::v5::GetCertRequest request;
    iamanager::v5::CertInfo       certInfoResponse;

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

    LOG_DBG() << "Certificate received: certURL=" << resCert.mCertURL.CStr() << ", keyURL=" << resCert.mKeyURL.CStr();

    return ErrorEnum::eNone;
}

Error PublicServiceHandler::GetNodeInfo(NodeInfo& nodeInfo) const
{
    LOG_DBG() << "Get node info";

    auto ctx = std::make_unique<grpc::ClientContext>();
    ctx->set_deadline(std::chrono::system_clock::now() + cIAMPublicServiceTimeout);

    iamanager::v5::NodeInfo nodeInfoResponse;

    if (auto status = mStub->GetNodeInfo(ctx.get(), google::protobuf::Empty {}, &nodeInfoResponse); !status.ok()) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eRuntime, status.error_message().c_str()));
    }

    if (auto err = pbconvert::ConvertToAos(nodeInfoResponse, nodeInfo); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error PublicServiceHandler::SetNodeStatus(const NodeStatus& status)
{
    LOG_DBG() << "Setting node status: status=" << status;

    return ErrorEnum::eNotSupported;
}

Error PublicServiceHandler::SubscribeNodeStatusChanged(
    [[maybe_unused]] iam::nodeinfoprovider::NodeStatusObserverItf& observer)
{
    LOG_DBG() << "Subscribing to node status changed";

    return ErrorEnum::eNotSupported;
}

Error PublicServiceHandler::UnsubscribeNodeStatusChanged(
    [[maybe_unused]] iam::nodeinfoprovider::NodeStatusObserverItf& observer)
{
    LOG_DBG() << "Unsubscribing from node status changed";

    return ErrorEnum::eNotSupported;
}

PublicServiceHandler::~PublicServiceHandler()
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
    const String& certType, iam::certhandler::CertReceiverItf& certReceiver)
{
    std::lock_guard lock {mMutex};

    LOG_INF() << "Subscribe to certificate changed: certType=" << certType;

    auto& subscription = mSubscriptions[certType.CStr()];

    if (!subscription.mSubscribers.insert(&certReceiver).second) {
        return Error(ErrorEnum::eAlreadyExist, "subscriber already exists for this cert type");
    }

    if (subscription.mSubscribers.size() == 1) {
        subscription.mFuture
            = std::async(std::launch::async, &PublicServiceHandler::RunTask, this, certType.CStr(), &subscription);
    }

    return ErrorEnum::eNone;
}

Error PublicServiceHandler::UnsubscribeCertChanged(iam::certhandler::CertReceiverItf& certReceiver)
{
    for (auto it = mSubscriptions.begin(); it != mSubscriptions.end();) {
        {
            std::lock_guard lock {mMutex};

            auto& subscription = it->second;

            if (subscription.mSubscribers.erase(&certReceiver) != 0) {
                LOG_INF() << "Unsubscribe from certificate changed: certType=" << it->first.c_str();
            }

            if (!subscription.mSubscribers.empty()) {
                it++;
                continue;
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

            it = mSubscriptions.erase(it);
        }
    }

    return ErrorEnum::eNone;
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
        return AOS_ERROR_WRAP(utils::ToAosError(e, ErrorEnum::eRuntime));
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
                LOG_INF() << "Certificate changed: certURL=" << certInfo.cert_url().c_str()
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
            LOG_ERR() << "Subscription loop failed: err=" << AOS_ERROR_WRAP(utils::ToAosError(e));
        }

        {
            std::unique_lock lock {mMutex};

            mCV.wait_for(lock, cReconnectInterval, [this, subscription]() { return subscription->mClose; });
        }
    }

    LOG_DBG() << "Subscription task stopped: certType=" << certType.c_str();
}

} // namespace aos::common::iamclient
