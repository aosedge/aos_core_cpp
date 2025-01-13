/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef NETWORKMANAGER_HPP_
#define NETWORKMANAGER_HPP_

#include <aos/sm/networkmanager.hpp>

namespace aos::sm::networkmanager {

/**
 * Namespace manager instance.
 */
class NamespaceManager : public NamespaceManagerItf {
public:
    /**
     * Creates network namespace.
     * @param instanceID instance ID.
     * @return Error.
     */
    Error CreateNetworkNamespace(const String& instanceID) override
    {
        (void)instanceID;

        return ErrorEnum::eNone;
    }

    /**
     * Returns network namespace path.
     *
     * @param instanceID instance ID.
     * @return RetWithError<StaticString<cFilePathLen>>.
     */
    RetWithError<StaticString<cFilePathLen>> GetNetworkNamespacePath(const String& instanceID) const override
    {
        (void)instanceID;

        return {""};
    }

    /**
     * Deletes network namespace.
     *
     * @param instanceID instance ID.
     * @return Error.
     */
    Error DeleteNetworkNamespace(const String& instanceID) override
    {
        (void)instanceID;

        return ErrorEnum::eNone;
    }
};

/**
 * Network manager provider interface.
 */
class NetworkInterfaceManager : public NetworkInterfaceManagerItf {
public:
    /**
     * Removes interface.
     *
     * @param ifname interface name.
     * @return Error.
     */
    Error RemoveInterface(const String& ifname) override
    {
        (void)ifname;

        return ErrorEnum::eNone;
    }

    /**
     * Brings up interface.
     *
     * @param ifname interface name.
     * @return Error.
     */
    Error BringUpInterface(const String& ifname) override
    {
        (void)ifname;

        return ErrorEnum::eNone;
    }
};

} // namespace aos::sm::networkmanager

#endif
