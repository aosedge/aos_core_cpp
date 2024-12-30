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

#include <grpcpp/security/credentials.h>
#include <iamanager/v5/iamanager.grpc.pb.h>

#include <aos/common/crypto/crypto.hpp>
#include <aos/common/crypto/utils.hpp>
#include <aos/common/tools/error.hpp>
#include <aos/iam/certhandler.hpp>
#include <utils/grpchelper.hpp>

namespace aos::common::iamclient {

/**
 * Configuration.
 */
struct Config {
    std::string mIAMPublicServerURL;
    std::string mCACert;
};

/**
 * Certificate provider interface.
 */
class CertProviderItf {
public:
    /**
     * Destructor.
     */
    virtual ~CertProviderItf() = default;

    /**
     * Gets MTLS configuration.
     *
     * @param certStorage Certificate storage.
     * @return MTLS configuration.
     */
    virtual RetWithError<std::shared_ptr<grpc::ChannelCredentials>> GetMTLSConfig(const std::string& certStorage) = 0;

    /**
     * Gets TLS credentials.
     *
     * @return TLS credentials.
     */
    virtual std::shared_ptr<grpc::ChannelCredentials> GetTLSCredentials() = 0;

    /**
     * Gets certificate.
     *
     * @param certType Certificate type.
     * @param certInfo Certificate info.
     * @return Error.
     */
    virtual Error GetCertificate(const std::string& certType, iam::certhandler::CertInfo& certInfo) = 0;

    /**
     * Subscribe to certificate changed.
     *
     * @param certType Certificate type.
     * @return Error.
     */
    virtual Error SubscribeCertChanged(const std::string& certType, iam::certhandler::CertReceiverItf& subscriber) = 0;

    /**
     * Unsubscribe to certificate changed.
     *
     * @param certType Certificate type.
     * @param subscriber Subscriber.
     * @return Error.
     */
    virtual void UnsubscribeCertChanged(const std::string& certType, iam::certhandler::CertReceiverItf& subscriber) = 0;
};

/**
 * MTLS credentials function.
 */
using MTLSCredentialsFunc = std::function<std::shared_ptr<grpc::ChannelCredentials>(
    const iam::certhandler::CertInfo&, const String&, crypto::CertLoaderItf&, crypto::x509::ProviderItf&)>;

/**
 * Public service handler.
 */
class PublicServiceHandler : public CertProviderItf {
public:
    /**
     * Default constructor.
     */
    PublicServiceHandler() = default;

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
     * @return MTLS configuration.
     */
    RetWithError<std::shared_ptr<grpc::ChannelCredentials>> GetMTLSConfig(const std::string& certStorage) override;

    /**
     * Gets TLS credentials.
     *
     * @return TLS credentials.
     */
    std::shared_ptr<grpc::ChannelCredentials> GetTLSCredentials() override;

    /**
     * Gets certificate.
     *
     * @param certType Certificate type.
     * @param certInfo Certificate info.
     * @return Error.
     */
    Error GetCertificate(const std::string& certType, iam::certhandler::CertInfo& certInfo) override;

    /**
     * Close handler
     */
    void Close();

    /**
     * Subscribe to certificate changed.
     *
     * @param certType Certificate type.
     * @return Error.
     */
    Error SubscribeCertChanged(const std::string& certType, iam::certhandler::CertReceiverItf& subscriber) override;

    /**
     * Unsubscribe to certificate changed.
     *
     * @param certType Certificate type.
     * @param subscriber Subscriber.
     * @return Error.
     */
    void UnsubscribeCertChanged(const std::string& certType, iam::certhandler::CertReceiverItf& subscriber) override;

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
