/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef NAMESPACEMANAGER_HPP_
#define NAMESPACEMANAGER_HPP_

#include <filesystem>

#include <aos/sm/networkmanager.hpp>

namespace aos::common::network {

/**
 * Namespace manager.
 */
class NamespaceManager : public sm::networkmanager::NamespaceManagerItf {
public:
    /**
     * Initializes namespace manager.
     *
     * @param netIf network interface manager.
     * @return Error.
     */
    Error Init(sm::networkmanager::NetworkInterfaceManagerItf& netIf);

    /**
     * Creates network namespace.
     * @param ns namespace name.
     * @return Error.
     */
    Error CreateNetworkNamespace(const String& ns) override;

    /**
     * Returns network namespace path.
     *
     * @param ns namespace name.
     * @return RetWithError<StaticString<cFilePathLen>>.
     */
    RetWithError<StaticString<cFilePathLen>> GetNetworkNamespacePath(const String& ns) const override;

    /**
     * Deletes network namespace.
     *
     * @param ns namespace name.
     * @return Error.
     */
    Error DeleteNetworkNamespace(const String& ns) override;

private:
    static constexpr auto cPathToNetNs  = "/run/netns";
    static constexpr auto cNSLen        = 64;
    static constexpr auto cNSPathFormat = "/proc/%d/task/%d/ns/net";

    sm::networkmanager::NetworkInterfaceManagerItf* mNetIf = nullptr;
};

} // namespace aos::common::network

#endif // NAMESPACEMANAGER_HPP_
