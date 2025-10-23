/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_IAMCLIENT_PUBLICIDENTITYSERVICE_HPP_
#define AOS_COMMON_IAMCLIENT_PUBLICIDENTITYSERVICE_HPP_

#include <memory>
#include <mutex>
#include <string>

#include <iamanager/v6/iamanager.grpc.pb.h>

#include <common/utils/grpcsubscriptionmanager.hpp>
#include <core/common/iamclient/itf/identprovider.hpp>
#include <core/common/tools/error.hpp>

#include "itf/tlscredentials.hpp"

namespace aos::common::iamclient {

// Type alias for Subjects subscription manager
using SubjectsSubscriptionManager = utils::GRPCSubscriptionManager<iamanager::v6::IAMPublicIdentityService::Stub,
    aos::iamclient::SubjectsListenerItf, iamanager::v6::Subjects, SubjectArray, google::protobuf::Empty>;

/**
 * Public identity service.
 */
class PublicIdentityService : public aos::iamclient::IdentProviderItf {
public:
    /**
     * Destructor
     */
    ~PublicIdentityService();

    /**
     * Initializes public identity service.
     * @param iamPublicServerURL IAM public server URL.
     * @param tlsCredentials TLS credentials.
     * @param insecureConnection whether to use insecure connection.
     * @return Error.
     */
    Error Init(
        const std::string& iamPublicServerURL, TLSCredentialsItf& tlsCredentials, bool insecureConnection = false);

    /**
     * Returns System info.
     *
     * @param[out] info result system info.
     * @returns Error.
     */
    Error GetSystemInfo(SystemInfo& info) override;

    /**
     * Returns subjects.
     *
     * @param[out] subjects result subjects.
     * @returns Error.
     */
    Error GetSubjects(Array<StaticString<cIDLen>>& subjects) override;

    /**
     * Subscribes subjects listener.
     *
     * @param subjectsListener subjects listener.
     * @returns Error.
     */
    Error SubscribeListener(aos::iamclient::SubjectsListenerItf& subjectsListener) override;

    /**
     * Unsubscribes subjects listener.
     *
     * @param subjectsListener subjects listener.
     * @returns Error.
     */
    Error UnsubscribeListener(aos::iamclient::SubjectsListenerItf& subjectsListener) override;

    /**
     * Reconnects to the server.
     * Note: Active subscription will be reconnected automatically.
     *
     * @returns Error.
     */
    Error Reconnect();

private:
    static constexpr auto cServiceTimeout = std::chrono::seconds(10);

    std::string                                                    mIAMPublicServerURL;
    bool                                                           mInsecureConnection {false};
    std::shared_ptr<grpc::ChannelCredentials>                      mCredentials;
    std::unique_ptr<iamanager::v6::IAMPublicIdentityService::Stub> mStub;
    TLSCredentialsItf*                                             mTLSCredentials {};
    std::mutex                                                     mMutex;
    std::unique_ptr<SubjectsSubscriptionManager>                   mSubscriptionManager;
};

} // namespace aos::common::iamclient

#endif
