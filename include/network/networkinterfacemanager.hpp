/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef NETWORKINTERFACEMANAGER_HPP_
#define NETWORKINTERFACEMANAGER_HPP_

#include <aos/sm/networkmanager.hpp>

namespace aos::common::network {

/**
 * Network interface manager.
 */
class NetworkInterfaceManager : public sm::networkmanager::NetworkInterfaceManagerItf {
public:
    /**
     * Removes interface.
     *
     * @param ifname interface name.
     * @return Error.
     */
    Error RemoveInterface(const String& ifname) override;

    /**
     * Brings up interface.
     *
     * @param ifname interface name.
     * @return Error.
     */
    Error BringUpInterface(const String& ifname) override;
};

} // namespace aos::common::network

#endif // NETWORKINTERFACEMANAGER_HPP_
