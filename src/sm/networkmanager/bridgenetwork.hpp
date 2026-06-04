/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_NETWORKMANAGER_BRIDGENETWORK_HPP_
#define AOS_SM_NETWORKMANAGER_BRIDGENETWORK_HPP_

#include <core/sm/networkmanager/itf/bridgenetwork.hpp>
#include <core/sm/networkmanager/itf/interfacemanager.hpp>

namespace aos::sm::networkmanager {

/**
 * Native bridge + veth implementation of BridgeNetworkItf.
 *
 * Replaces the CNI bridge plugin: creates a veth pair per instance,
 * attaches the host end to an existing bridge, moves the peer end into
 * the instance netns, and configures IP / route / hairpin. Masquerade is a
 * per-network rule owned by NetworkManager, not by this per-instance attach.
 */
class BridgeNetwork : public BridgeNetworkItf {
public:
    /**
     * Initializes the bridge network.
     *
     * @param netIf interface manager.
     * @return Error.
     */
    Error Init(InterfaceManagerItf& netIf);

    /**
     * Attaches an instance to the bridge.
     *
     * @param instanceID instance id.
     * @param params bridge parameters.
     * @param[out] result attach result.
     * @return Error.
     */
    Error Attach(const String& instanceID, const BridgeParams& params, BridgeAttachResult& result) override;

    /**
     * Detaches an instance from the bridge.
     *
     * @param instanceID instance id.
     * @param bridgeIfName bridge interface name.
     * @return Error.
     */
    Error Detach(const String& instanceID, const String& bridgeIfName) override;

private:
    static StaticString<cInterfaceLen> VethName(const String& instanceID, const String& prefix);

    InterfaceManagerItf* mNetIf {};
};

} // namespace aos::sm::networkmanager

#endif
