/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_IAMCLIENT_PROVISIONINGSERVICE_HPP_
#define AOS_COMMON_IAMCLIENT_PROVISIONINGSERVICE_HPP_

#include <memory>
#include <mutex>

#include <iamanager/v6/iamanager.grpc.pb.h>

#include <core/common/iamclient/itf/provisioning.hpp>

#include "itf/tlscredentials.hpp"

namespace aos::common::iamclient {

/**
 * Provisioning service.
 */
class ProvisioningService : public aos::iamclient::ProvisioningItf {
public:
    /**
     * Initializes certificate service.
     *
     * @param iamProtectedServerURL IAM protected server URL.
     * @param certStorage certificate storage.
     * @param tlsCredentials TLS credentials.
     * @param insecureConnection use insecure connection.
     * @return Error.
     */
    Error Init(const std::string& iamProtectedServerURL, const std::string& certStorage,
        TLSCredentialsItf& tlsCredentials, bool insecureConnection = false);

    /**
     * Returns IAM cert types.
     *
     * @param nodeID node ID.
     * @param[out] certTypes result certificate types.
     * @returns Error.
     */
    Error GetCertTypes(const String& nodeID, Array<StaticString<cCertTypeLen>>& certTypes) const override;

    /**
     * Starts node provisioning.
     *
     * @param nodeID node ID.
     * @param password password.
     * @returns Error.
     */
    Error StartProvisioning(const String& nodeID, const String& password) override;

    /**
     * Finishes node provisioning.
     *
     * @param nodeID node ID.
     * @param password password.
     * @returns Error.
     */
    Error FinishProvisioning(const String& nodeID, const String& password) override;

    /**
     * Deprovisions node.
     *
     * @param nodeID node ID.
     * @param password password.
     * @returns Error.
     */
    Error Deprovision(const String& nodeID, const String& password) override;

    /**
     * Reconnects to the server.
     *
     * @returns Error.
     */
    Error Reconnect();

private:
    static constexpr auto cServiceTimeout = std::chrono::seconds(10);

    std::string                                                  mIAMProtectedServerURL;
    std::string                                                  mCertStorage;
    bool                                                         mInsecureConnection {false};
    std::shared_ptr<grpc::ChannelCredentials>                    mCredentials;
    TLSCredentialsItf*                                           mTLSCredentials {};
    std::unique_ptr<iamanager::v6::IAMProvisioningService::Stub> mStub;
    mutable std::mutex                                           mMutex;
};

} // namespace aos::common::iamclient

#endif
