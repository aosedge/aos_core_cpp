/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_IAMCLIENT_ITF_PERMSERVICE_HPP_
#define AOS_COMMON_IAMCLIENT_ITF_PERMSERVICE_HPP_

#include <core/iam/permhandler/permhandler.hpp>

namespace aos::common::iamclient {

/**
 * Permissions service interface.
 */
class PermissionsServiceItf {
public:
    /**
     * Adds new service instance and its permissions into cache.
     *
     * @param instanceIdent instance identification.
     * @param instancePermissions instance permissions.
     * @returns RetWithError<StaticString<cSecretLen>>.
     */
    virtual RetWithError<StaticString<iam::permhandler::cSecretLen>> RegisterInstance(
        const InstanceIdent& instanceIdent, const Array<FunctionServicePermissions>& instancePermissions)
        = 0;

    /**
     * Unregisters instance deletes service instance with permissions from cache.
     *
     * @param instanceIdent instance identification.
     * @returns Error.
     */
    virtual Error UnregisterInstance(const InstanceIdent& instanceIdent) = 0;

    /**
     * Returns instance ident and permissions by secret and functional server ID.
     *
     * @param secret secret.
     * @param funcServerID functional server ID.
     * @param[out] instanceIdent result instance ident.
     * @param[out] servicePermissions result service permission.
     * @returns Error.
     */
    virtual Error GetPermissions(const String& secret, const String& funcServerID, InstanceIdent& instanceIdent,
        Array<FunctionPermissions>& servicePermissions)
        = 0;
};

} // namespace aos::common::iamclient

#endif
