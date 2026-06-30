/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_NETWORK_INTERFACEMANAGER_HPP_
#define AOS_COMMON_NETWORK_INTERFACEMANAGER_HPP_

#include <functional>
#include <optional>
#include <string>

#include <sched.h>
#include <sys/socket.h>
#include <unistd.h>

#include <core/common/crypto/itf/crypto.hpp>
#include <core/sm/networkmanager/itf/interfacefactory.hpp>
#include <core/sm/networkmanager/itf/interfacemanager.hpp>

#include "utils.hpp"

// Forward declarations
struct nl_sock;
struct rtnl_link;

namespace aos::common::network {

/**
 * Link attributes.
 */
struct LinkAttrs {
    std::string mName;
    int         mParentIndex {};
    int         mMasterIndex {};
    int         mTxQLen {};
    std::string mMac;
};

/**
 * IP address.
 */
struct IPAddr {
    std::string mIP;
    std::string mSubnet;
    int         mFamily = AF_INET;
    std::string mLabel;
};

/**
 * Link interface.
 */
class LinkItf {
public:
    /**
     * Destructor.
     */
    virtual ~LinkItf() = default;

    /**
     * Gets link attributes.
     *
     * @return Link attributes.
     */
    virtual const LinkAttrs& GetAttrs() const = 0;

    /**
     * Gets link type.
     *
     * @return Link type.
     */
    virtual const char* GetType() const = 0;
};

/**
 * Bridge link.
 */
class Bridge : public LinkItf {
public:
    /**
     * Construct a new Bridge object.
     */
    explicit Bridge(const LinkAttrs& attrs);

    /**
     * Get the attributes of the link.
     *
     * @return Link attributes.
     */
    const LinkAttrs& GetAttrs() const override;

    /**
     * Get the type of the link.
     *
     * @return Link type.
     */
    const char* GetType() const override;

private:
    LinkAttrs mAttrs;
};

/**
 * Vlan link.
 */
class Vlan : public LinkItf {
public:
    /**
     * Construct a new Vlan object.
     */
    Vlan(const LinkAttrs& attrs, int vlanId);

    /**
     * Get the attributes of the link.
     *
     * @return Link attributes.
     */
    const LinkAttrs& GetAttrs() const override;

    /**
     * Get the type of the link.
     *
     * @return Link type.
     */
    const char* GetType() const override;

    /**
     * Get the vlan id.
     *
     * @return Vlan id.
     */
    int GetVlanId() const;

private:
    LinkAttrs mAttrs;
    int       mVlanId {-1};
};

/**
 * Network interface manager.
 */
class InterfaceManager : public sm::networkmanager::InterfaceManagerItf,
                         public sm::networkmanager::InterfaceFactoryItf {
public:
    /**
     * Initializes interface manager.
     *
     * @param random random.
     * @return Error.
     */
    Error Init(crypto::RandomItf& random);

    /**
     * Removes interface.
     *
     * @param ifname interface name.
     * @return Error.
     */
    Error DeleteLink(const String& ifname) override;

    /**
     * Brings up interface.
     *
     * @param ifname interface name.
     * @param netNSPath optional path to the netns; empty for current.
     * @return Error.
     */
    Error SetupLink(const String& ifname, const String& netNSPath = "") override;

    /**
     * Sets master.
     *
     * @param ifname interface name.
     * @param master master.
     * @return Error.
     */
    Error SetMasterLink(const String& ifname, const String& master) override;

    /**
     * Creates bridge.
     *
     * @param name bridge name.
     * @param ip ip.
     * @param subnet subnet.
     * @return Error.
     */
    Error CreateBridge(const String& name, const String& ip, const String& subnet) override;

    /**
     * Creates vlan.
     *
     * @param name vlan name.
     * @param vlanId vlan id.
     * @return Error.
     */
    Error CreateVlan(const String& name, uint64_t vlanId, const String& master) override;

    /**
     * Creates a parameter-less link of the given kind.
     *
     * @param name link name.
     * @param kind link kind (rtnetlink link-type string, e.g. "ifb").
     * @return Error.
     */
    Error CreateLink(const String& name, const String& kind) override;

    /**
     * Adds link.
     *
     * @param link link.
     * @return Error.
     */
    Error AddLink(const LinkItf* link);

    /**
     * Adds address.
     *
     * @param ifname interface name.
     * @param addr address.
     * @return Error.
     */
    Error AddAddr(const String& ifname, const IPAddr& addr);

    /**
     * Deletes address.
     *
     * @param ifname interface name.
     * @param addr address.
     * @return Error.
     */
    Error DeleteAddr(const String& ifname, const IPAddr& addr);

    /**
     * Gets address list.
     *
     * @param ifname interface name.
     * @param family address family.
     * @param[out] addr address list.
     * @return Error.
     */
    Error GetAddrList(const String& ifname, int family, Array<IPAddr>& addr) const;

    /**
     * Creates a veth pair. Both ends are initially in the current netns.
     *
     * @param hostIfName host-side veth name.
     * @param peerIfName peer-side veth name.
     * @return Error.
     */
    Error CreateVeth(const String& hostIfName, const String& peerIfName) override;

    /**
     * Creates a veth pair with the peer placed directly into the given netns,
     * already named peerIfName. Combines create + move + rename into one op.
     *
     * @param hostIfName host-side veth name (current namespace).
     * @param peerIfName peer-side veth name inside the target namespace.
     * @param netNSPath path to the target netns.
     * @return Error.
     */
    Error CreateVethToNamespace(
        const String& hostIfName, const String& peerIfName, const String& netNSPath, const String& master) override;

    /**
     * Brings up, addresses and default-routes an interface inside a netns in a
     * single namespace entry.
     *
     * @param ifname interface name inside the namespace.
     * @param ipWithMask IP in CIDR form, e.g. "10.0.0.5/24".
     * @param gateway default-route gateway IP.
     * @param netNSPath path to the netns; empty for current.
     * @return Error.
     */
    Error ConfigureInstanceInterface(
        const String& ifname, const String& ipWithMask, const String& gateway, const String& netNSPath) override;

    /**
     * Moves a link into a network namespace.
     *
     * @param ifname interface name.
     * @param netNSPath path to the target netns (e.g. /run/netns/<id>).
     * @return Error.
     */
    Error MoveLinkToNamespace(const String& ifname, const String& netNSPath) override;

    /**
     * Renames a link (must be down). Runs inside netNSPath when non-empty.
     *
     * @param ifname current interface name.
     * @param newName new interface name.
     * @param netNSPath optional path to the netns; empty for current.
     * @return Error.
     */
    Error RenameLink(const String& ifname, const String& newName, const String& netNSPath) override;

    /**
     * Assigns an IP address in CIDR form to an interface, optionally inside a netns.
     *
     * @param ifname interface name.
     * @param ipWithMask IP in CIDR form, e.g. "10.0.0.5/24".
     * @param netNSPath path to the netns; empty for current.
     * @return Error.
     */
    Error AddAddress(const String& ifname, const String& ipWithMask, const String& netNSPath) override;

    /**
     * Adds a route, optionally inside a netns.
     *
     * @param destination destination prefix in CIDR form ("0.0.0.0/0" for default).
     * @param gateway gateway IP.
     * @param netNSPath path to the netns; empty for current.
     * @return Error.
     */
    Error AddRoute(const String& destination, const String& gateway, const String& netNSPath) override;

    /**
     * Enables/disables hairpin mode on a bridge port via sysfs.
     *
     * @param ifname host-side veth interface name.
     * @param enable enable/disable.
     * @return Error.
     */
    Error SetHairpin(const String& ifname, bool enable) override;

private:
    using LinkDeleter = std::function<void(rtnl_link*)>;
    using UniqueLink  = std::unique_ptr<rtnl_link, LinkDeleter>;

    RetWithError<int>        GetMasterInterfaceIndex() const;
    RetWithError<UniqueLink> AllocLink() const;

    crypto::RandomItf* mRandom {};
};

} // namespace aos::common::network

#endif
