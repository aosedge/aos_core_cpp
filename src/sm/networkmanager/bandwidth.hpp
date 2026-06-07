/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_NETWORKMANAGER_BANDWIDTH_HPP_
#define AOS_SM_NETWORKMANAGER_BANDWIDTH_HPP_

#include <core/common/tools/noncopyable.hpp>
#include <core/common/types/network.hpp>
#include <core/sm/networkmanager/itf/bandwidth.hpp>
#include <core/sm/networkmanager/itf/interfacefactory.hpp>
#include <core/sm/networkmanager/itf/interfacemanager.hpp>

#include <common/network/itf/tcbackend.hpp>

namespace aos::sm::networkmanager {

/**
 * Native BandwidthItf implementation.
 *
 * Installs a root TBF qdisc on the host-side veth (host-veth egress =
 * traffic into the container) and an IFB pseudo-device with a mirred
 * ingress redirect + root TBF on the IFB (host-veth ingress = traffic
 * out of the container).
 *
 * The IFB device name is derived deterministically from the host-veth
 * name so Clear can find and remove it without persisted state.
 */
class Bandwidth : public BandwidthItf, private NonCopyable {
public:
    /**
     * Initializes bandwidth.
     *
     * @param tc tc backend.
     * @param ifFactory interface factory (creates the IFB device).
     * @param ifMgr interface manager (brings up / removes the IFB device).
     * @return error.
     */
    Error Init(common::network::TCBackendItf& tc, InterfaceFactoryItf& ifFactory, InterfaceManagerItf& ifMgr);

    /**
     * Applies bandwidth shaping to a host-side veth.
     *
     * @param ifName host-side veth name.
     * @param params bandwidth parameters.
     * @return error.
     */
    Error Apply(const String& ifName, const BandwidthParams& params) override;

    /**
     * Removes shaping previously installed by Apply. Idempotent: returns
     * eNone if nothing is installed.
     *
     * @param ifName host-side veth name.
     * @return error.
     */
    Error Clear(const String& ifName) override;

private:
    static StaticString<cInterfaceLen> IFBName(const String& hostIfName);

    common::network::TCBackendItf* mTC {};
    InterfaceFactoryItf*           mIfFactory {};
    InterfaceManagerItf*           mIfMgr {};
};

} // namespace aos::sm::networkmanager

#endif
