/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_IAMCLIENT_PUBLICSERVICEHANDLER_HPP_
#define AOS_COMMON_IAMCLIENT_PUBLICSERVICEHANDLER_HPP_

#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

#include <iamanager/v5/iamanager.grpc.pb.h>

#include <grpcpp/security/credentials.h>

#include "itf/publicservice.hpp"
#include "itf/tlscredentials.hpp"
#include "subscriptionmanager.hpp"

namespace aos::common::iamclient {

/**
 * Public service implementation.
 */
class PublicService : public PublicServiceItf {
public:
    /**
     * Destructor - ensures all subscription managers are closed before stub destruction.
     */
    ~PublicService();

    /**
     * Initializes service.
     *
     * @param iamPublicServerURL IAM public server URL.
     * @return Error.
     */
    Error Init(const std::string& iamPublicServerURL, const std::string& CACert, TLSCredentialsItf& tlsCredentials,
        bool insecureConnection = false);

    /**
     * Returns certificate info.
     *
     * @param certType certificate type.
     * @param issuer issuer name.
     * @param serial serial number.
     * @param[out] resCert result certificate.
     * @returns Error.
     */
    Error GetCert(const String& certType, const Array<uint8_t>& issuer, const Array<uint8_t>& serial,
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

    /**
     * Gets the node info object.
     *
     * @param[out] nodeInfo node info
     * @return Error
     */
    Error GetNodeInfo(NodeInfoObsolete& nodeInfo) const override;

private:
    static constexpr auto cIAMPublicServiceTimeout = std::chrono::seconds(10);

    Error CreateCredentials(bool insecureConnection, const std::string& CACert);

    std::shared_ptr<grpc::ChannelCredentials>              mCredentials;
    std::unique_ptr<iamanager::v5::IAMPublicService::Stub> mStub;
    std::string                                            mIAMPublicServerURL;
    TLSCredentialsItf*                                     mTLSCredentials {};

    std::mutex                                                            mMutex;
    std::unordered_map<std::string, std::unique_ptr<SubscriptionManager>> mSubscriptions;
};

} // namespace aos::common::iamclient

#endif
