/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PERMSERVICEHANDLER_HPP_
#define PERMSERVICEHANDLER_HPP_

#include <chrono>
#include <memory>
#include <string>

#include <aos/iam/permhandler.hpp>

#include "publicservicehandler.hpp"

namespace aos::common::iamclient {
/**
 * Permissions service handler.
 */
class PermissionsServiceHandler : public iam::permhandler::PermHandlerItf {
public:
    /**
     * Initializes permissions service handler.
     *
     * @param IAMProtectedServerURL IAM protected server URL.
     * @param certStorage certificate storage.
     * @param mTLSCredentials TLS credentials.
     * @return Error.
     */
    Error Init(
        const std::string& IAMProtectedServerURL, const std::string& certStorage, TLSCredentialsItf& TLSCredentials);

    /**
     * Adds new service instance and its permissions into cache.
     *
     * @param instanceIdent instance identification.
     * @param instancePermissions instance permissions.
     * @returns RetWithError<StaticString<cSecretLen>>.
     */
    RetWithError<StaticString<iam::permhandler::cSecretLen>> RegisterInstance(
        const InstanceIdent& instanceIdent, const Array<FunctionServicePermissions>& instancePermissions) override;

    /**
     * Unregisters instance deletes service instance with permissions from cache.
     *
     * @param instanceIdent instance identification.
     * @returns Error.
     */
    Error UnregisterInstance(const InstanceIdent& instanceIdent) override;

    /**
     * Returns instance ident and permissions by secret and functional server ID.
     *
     * @param secret secret.
     * @param funcServerID functional server ID.
     * @param[out] instanceIdent result instance ident.
     * @param[out] servicePermissions result service permission.
     * @returns Error.
     */
    Error GetPermissions(const String& secret, const String& funcServerID, InstanceIdent& instanceIdent,
        Array<FunctionPermissions>& servicePermissions) override;

private:
    static constexpr auto cIAMPermissionsServiceTimeout = std::chrono::seconds(10);

    std::shared_ptr<grpc::ChannelCredentials> CreateCredentials();

    TLSCredentialsItf* mTLSCredentials {};
    std::string        mIAMProtectedServerURL;
    std::string        mCertStorage;
};

} // namespace aos::common::iamclient

#endif // PERMSERVICEHANDLER_HPP_
