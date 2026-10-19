/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_IAMCLIENT_PUBLICSERVICEHANDLER_HPP_
#define AOS_COMMON_IAMCLIENT_PUBLICSERVICEHANDLER_HPP_

#include <condition_variable>
#include <future>
#include <mutex>
#include <thread>
#include <unordered_map>

#include <iamanager/v7/iamanager.grpc.pb.h>

#include <common/utils/grpcsubscriptionmanager.hpp>
#include <core/common/iamclient/itf/certprovider.hpp>
#include <core/common/tools/error.hpp>

#include "itf/tlscredentials.hpp"

namespace aos::common::iamclient {

// Type alias for cert key subscription manager
using CertKeySubscriptionManager
    = utils::GRPCSubscriptionManager<iamanager::v7::IAMPublicCertificateService::Stub, aos::iamclient::CertListenerItf,
        iamanager::v7::CertKeyInfo, iamanager::v7::CertKeyInfo, iamanager::v7::SubscribeCertKeyChangedRequest>;

/**
 * Public cert service.
 */
class PublicCertService : public aos::iamclient::CertProviderItf {
public:
    /**
     * Destructor
     */
    ~PublicCertService();

    /**
     * Initializes service.
     *
     * @param iamPublicServerURL IAM public server URL.
     * @return Error.
     */
    Error Init(
        const std::string& iamPublicServerURL, TLSCredentialsItf& tlsCredentials, bool insecureConnection = false);

    /**
     * Returns certificate info.
     *
     * @param storageID certificate storage ID.
     * @param issuer issuer name.
     * @param serial serial number.
     * @param[out] resCert result certificate.
     * @returns Error.
     */
    Error GetCert(const String& storageID, const Array<uint8_t>& issuer, const Array<uint8_t>& serial,
        CertInfo& resCert) const override;

    /**
     * Returns trusted root certificates.
     *
     * @param[out] resCerts result certificates.
     * @returns Error.
     */
    Error GetRootCerts(Array<CertInfo>& resCerts) const override;

    /**
     * Subscribes certificates receiver.
     *
     * @param storageID certificate storage ID.
     * @param certReceiver certificate receiver.
     * @returns Error.
     */
    Error SubscribeListener(const String& storageID, ::aos::iamclient::CertListenerItf& certListener) override;

    /**
     * Subscribes trusted root certificates receiver.
     *
     * @param certListener certificate listener.
     * @returns Error.
     */
    Error SubscribeRootCertsListener(::aos::iamclient::CertListenerItf& certListener) override;

    /**
     * Unsubscribes certificate receiver.
     *
     * @param certReceiver certificate receiver.
     * @returns Error.
     */
    Error UnsubscribeListener(::aos::iamclient::CertListenerItf& certListener) override;

    /**
     * Reconnects to the server.
     * Note: All active subscriptions will be closed and must be resubscribed.
     *
     * @returns Error.
     */
    Error Reconnect();

private:
    static constexpr auto cServiceTimeout = std::chrono::seconds(10);

    std::string                                                       mIAMPublicServerURL;
    bool                                                              mInsecureConnection {false};
    std::shared_ptr<grpc::ChannelCredentials>                         mCredentials;
    std::unique_ptr<iamanager::v7::IAMPublicCertificateService::Stub> mStub;
    TLSCredentialsItf*                                                mTLSCredentials {};
    mutable std::mutex                                                mMutex;

    std::unordered_map<std::string, std::unique_ptr<CertKeySubscriptionManager>> mCertKeySubscriptions;
};

} // namespace aos::common::iamclient

#endif
