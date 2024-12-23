/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PUBLICSERVICEHANDLER_HPP_
#define PUBLICSERVICEHANDLER_HPP_

#include <condition_variable>
#include <future>
#include <mutex>
#include <thread>
#include <unordered_map>

#include <aos/common/crypto/crypto.hpp>
#include <aos/common/crypto/utils.hpp>
#include <aos/common/tools/error.hpp>
#include <aos/iam/certprovider.hpp>

#include <iamanager/v5/iamanager.grpc.pb.h>

#include "utils/grpchelper.hpp"

namespace aos::common::iamclient {

/**
 * Configuration.
 */
struct Config {
    std::string mIAMPublicServerURL;
    std::string mCACert;
};

class TLSCredentialsItf : public iam::certprovider::CertProviderItf {
public:
    /**
     * Gets MTLS configuration.
     *
     * @param certStorage Certificate storage.
     * @return MTLS credentials.
     */
    virtual RetWithError<std::shared_ptr<grpc::ChannelCredentials>> GetMTLSClientCredentials(const String& certStorage)
        = 0;

    /**
     * Gets TLS credentials.
     *
     * @return TLS credentials.
     */
    virtual RetWithError<std::shared_ptr<grpc::ChannelCredentials>> GetTLSClientCredentials() = 0;

    /**
     * Destructor.
     */
    virtual ~TLSCredentialsItf() = default;
};

/**
 * MTLS credentials function.
 */
using MTLSCredentialsFunc
    = std::function<std::shared_ptr<grpc::ChannelCredentials>(const iam::certhandler::CertInfo& certInfo,
        const String& rootCA, crypto::CertLoaderItf& certLoader, crypto::x509::ProviderItf& cryptoProvider)>;

/**
 * Public service handler.
 */
class PublicServiceHandler : public TLSCredentialsItf {
public:
    /**
     * Default constructor.
     */
    PublicServiceHandler() = default;

    /**
     * Destructor.
     */
    virtual ~PublicServiceHandler();

    /**
     * Initializes handler.
     *
     * @param cfg Configuration.
     * @param certLoader Certificate loader.
     * @param cryptoProvider Crypto provider.
     * @param insecureConnection Insecure connection.
     * @param mtlsCredentialsFunc MTLS credentials function.
     * @return Error.
     */
    Error Init(const Config& cfg, crypto::CertLoaderItf& certLoader, crypto::x509::ProviderItf& cryptoProvider,
        bool                insecureConnection  = false,
        MTLSCredentialsFunc mtlsCredentialsFunc = common::utils::GetMTLSClientCredentials);

    /**
     * Gets MTLS configuration.
     *
     * @param certStorage Certificate storage.
     * @return MTLS credentials.
     */
    RetWithError<std::shared_ptr<grpc::ChannelCredentials>> GetMTLSClientCredentials(
        const String& certStorage) override;

    /**
     * Gets TLS credentials.
     *
     * @return TLS credentials.
     */
    RetWithError<std::shared_ptr<grpc::ChannelCredentials>> GetTLSClientCredentials() override;

    /**
     * Returns certificate info.
     *
     * @param certType certificate type.
     * @param issuer issuer name.
     * @param serial serial number.
     * @param[out] resCert result certificate.
     * @returns Error.
     */
    virtual Error GetCert(const String& certType, const Array<uint8_t>& issuer, const Array<uint8_t>& serial,
        iam::certhandler::CertInfo& resCert) const override;

    /**
     * Subscribes certificates receiver.
     *
     * @param certType certificate type.
     * @param certReceiver certificate receiver.
     * @returns Error.
     */
    Error SubscribeCertChanged(const String& certType, iam::certhandler::CertReceiverItf& certReceiver) override;

    /**
     * Unsubscribes certificate receiver.
     *
     * @param certReceiver certificate receiver.
     * @returns Error.
     */
    Error UnsubscribeCertChanged(iam::certhandler::CertReceiverItf& certReceiver) override;

private:
    struct Subscription {
        Subscription() = default;

        explicit Subscription(std::future<void> future)
            : mFuture(std::move(future))
        {
        }

        std::unique_ptr<grpc::ClientContext> mCtx {};
        std::future<void>                    mFuture;
        bool                                 mClose {};

        std::unordered_set<iam::certhandler::CertReceiverItf*> mSubscribers;
    };

    static constexpr auto cIAMPublicServiceTimeout = std::chrono::seconds(10);
    static constexpr auto cReconnectInterval       = std::chrono::seconds(3);

    using IAMPublicServicePtr = std::unique_ptr<iamanager::v5::IAMPublicService::Stub>;

    Error CreateCredentials(bool insecureConnection);
    void  RunTask(const std::string& certType, Subscription* subscription);
    Error ProcessCertInfo(const iamanager::v5::CertInfo& info);

    Config                                    mConfig;
    crypto::CertLoaderItf*                    mCertLoader {};
    crypto::x509::ProviderItf*                mCryptoProvider {};
    std::shared_ptr<grpc::ChannelCredentials> mCredentials;
    MTLSCredentialsFunc                       mMTLSCredentialsFunc;
    std::mutex                                mMutex;
    std::condition_variable                   mCV;

    std::unique_ptr<iamanager::v5::IAMPublicService::Stub> mStub;
    std::unordered_map<std::string, Subscription>          mSubscriptions;
};

} // namespace aos::common::iamclient

#endif
