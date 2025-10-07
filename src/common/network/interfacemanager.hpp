/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_NETWORK_INTERFACEMANAGER_HPP_
#define AOS_COMMON_NETWORK_INTERFACEMANAGER_HPP_

#include <optional>
#include <string>

#include <sys/socket.h>

#include <core/common/crypto/itf/crypto.hpp>
#include <core/sm/networkmanager/networkmanager.hpp>

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
     * @return Error.
     */
    Error SetupLink(const String& ifname) override;

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
    Error CreateVlan(const String& name, uint64_t vlanId) override;

    // /**
    //  * Gets route list.
    //  *
    //  * @param[out] routes routes.
    //  * @return Error.
    //  */
    // Error GetRouteList(Array<RouteInfo>& routes) const;

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

private:
    using LinkDeleter = std::function<void(rtnl_link*)>;
    using UniqueLink  = std::unique_ptr<rtnl_link, LinkDeleter>;

    RetWithError<int>        GetMasterInterfaceIndex() const;
    RetWithError<UniqueLink> CreateLink() const;

    crypto::RandomItf* mRandom {};
};

} // namespace aos::common::network

#endif
