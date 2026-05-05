/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_NETWORKMANAGER_BRIDGENETWORK_HPP_
#define AOS_SM_NETWORKMANAGER_BRIDGENETWORK_HPP_

#include <core/sm/networkmanager/itf/bridgenetwork.hpp>
#include <core/sm/networkmanager/itf/firewall.hpp>
#include <core/sm/networkmanager/itf/interfacemanager.hpp>

namespace aos::sm::networkmanager {

/**
 * Native bridge + veth implementation of BridgeNetworkItf.
 *
 * Replaces the CNI bridge plugin: creates a veth pair per instance,
 * attaches the host end to an existing bridge, moves the peer end into
 * the instance netns, configures IP / route / hairpin, and delegates
 * IPMasq rules to FirewallItf.
 */
class BridgeNetwork : public BridgeNetworkItf {
public:
    /**
     * Initializes the bridge network.
     *
     * @param netIf interface manager.
     * @param firewall firewall interface (for IPMasq rules).
     * @return Error.
     */
    Error Init(InterfaceManagerItf& netIf, FirewallItf& firewall);

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
     * @param subnet subnet passed to Attach (for IPMasq cleanup).
     * @return Error.
     */
    Error Detach(const String& instanceID, const String& bridgeIfName, const String& subnet) override;

private:
    static StaticString<cInterfaceLen> HostVethName(const String& instanceID);

    InterfaceManagerItf* mNetIf {};
    FirewallItf*         mFirewall {};
};

} // namespace aos::sm::networkmanager

#endif
