/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_IAMCLIENT_PERMSERVICE_HPP_
#define AOS_COMMON_IAMCLIENT_PERMSERVICE_HPP_

#include <chrono>
#include <memory>
#include <mutex>
#include <string>

#include <iamanager/v6/iamanager.grpc.pb.h>

#include <core/common/iamclient/itf/permhandler.hpp>

#include "itf/tlscredentials.hpp"

namespace aos::common::iamclient {

/**
 * Permissions service.
 */
class PermissionsService : public aos::iamclient::PermHandlerItf {
public:
    /**
     * Initializes permissions service handler.
     *
     * @param iamProtectedServerURL IAM protected server URL.
     * @param certStorage certificate storage.
     * @param tlsCredentials TLS credentials.
     * @return Error.
     */
    Error Init(const std::string& iamProtectedServerURL, const std::string& certStorage,
        TLSCredentialsItf& tlsCredentials, bool insecureConnection = false);

    /**
     * Adds new service instance and its permissions into cache.
     *
     * @param instanceIdent instance identification.
     * @param instancePermissions instance permissions.
     * @returns RetWithError<StaticString<cSecretLen>>.
     */
    RetWithError<StaticString<cSecretLen>> RegisterInstance(
        const InstanceIdent& instanceIdent, const Array<FunctionServicePermissions>& instancePermissions) override;

    /**
     * Unregisters instance deletes service instance with permissions from cache.
     *
     * @param instanceIdent instance identification.
     * @returns Error.
     */
    Error UnregisterInstance(const InstanceIdent& instanceIdent) override;

    /**
     * Reconnects to the server.
     *
     * @returns Error.
     */
    Error Reconnect();

private:
    static constexpr auto cServiceTimeout = std::chrono::seconds(10);

    std::string                                                 mIAMProtectedServerURL;
    std::string                                                 mCertStorage;
    bool                                                        mInsecureConnection {false};
    std::shared_ptr<grpc::ChannelCredentials>                   mCredentials;
    TLSCredentialsItf*                                          mTLSCredentials {};
    std::unique_ptr<iamanager::v6::IAMPermissionsService::Stub> mStub;
    std::mutex                                                  mMutex;
};

} // namespace aos::common::iamclient

#endif
