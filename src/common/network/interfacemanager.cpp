/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <algorithm>
#include <iomanip>
#include <memory>
#include <random>
#include <sstream>
#include <string>

#include <arpa/inet.h>
#include <linux/if.h>
#include <linux/rtnetlink.h>
#include <netinet/in.h>
#include <netlink/netlink.h>
#include <netlink/route/addr.h>
#include <netlink/route/link.h>
#include <netlink/route/link/bridge.h>
#include <netlink/route/link/vlan.h>

#include <common/logger/logmodule.hpp>

#include "interfacemanager.hpp"

namespace aos::common::network {

namespace {

RetWithError<std::string> GenerateMACAddress(crypto::RandomItf& random)
{
    StaticArray<uint8_t, 6> mac;

    if (auto err = random.RandBuffer(mac, mac.Size()); !err.IsNone()) {
        return {std::string(), AOS_ERROR_WRAP(err)};
    }

    // Set the local bit
    mac[0] = (mac[0] & 0xFE) | 0x02;

    std::stringstream ss;

    for (size_t i = 0; i < mac.Size(); ++i) {
        if (i > 0) {
            ss << ":";
        }

        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(mac[i]);
    }

    return ss.str();
}

} // namespace

/***********************************************************************************************************************
 * Bridge
 **********************************************************************************************************************/

Bridge::Bridge(const LinkAttrs& attrs)
    : mAttrs(attrs)
{
}

const LinkAttrs& Bridge::GetAttrs() const
{
    return mAttrs;
}

const char* Bridge::GetType() const
{
    return "bridge";
}

Vlan::Vlan(const LinkAttrs& attrs, int vlanId)
    : mAttrs(attrs)
    , mVlanId(vlanId)
{
}

/***********************************************************************************************************************
 * Vlan
 **********************************************************************************************************************/

const LinkAttrs& Vlan::GetAttrs() const
{
    return mAttrs;
}

const char* Vlan::GetType() const
{
    return "vlan";
}

int Vlan::GetVlanId() const
{
    return mVlanId;
}

/***********************************************************************************************************************
 * InterfaceManager
 **********************************************************************************************************************/

Error InterfaceManager::Init(crypto::RandomItf& random)
{
    mRandom = &random;

    return ErrorEnum::eNone;
}

Error InterfaceManager::DeleteLink(const String& ifname)
{
    LOG_DBG() << "Remove interface: ifname=" << ifname;

    auto [sock, err] = CreateNetlinkSocket();
    if (!err.IsNone()) {
        return err;
    }

    auto [link, linkErr] = CreateLink();
    if (!linkErr.IsNone()) {
        return linkErr;
    }

    rtnl_link_set_name(link.get(), ifname.CStr());

    if (auto errLinkDel = rtnl_link_delete(sock.get(), link.get()); errLinkDel < 0) {
        return NLToAosErr(errLinkDel, "failed to delete link");
    }

    return ErrorEnum::eNone;
}

Error InterfaceManager::SetupLink(const String& ifname)
{
    LOG_DBG() << "Bring up interface: ifname=" << ifname;

    auto [sock, err] = CreateNetlinkSocket();
    if (!err.IsNone()) {
        return err;
    }

    auto [link, linkErr] = CreateLink();
    if (!linkErr.IsNone()) {
        return linkErr;
    }

    rtnl_link_set_name(link.get(), ifname.CStr());
    rtnl_link_set_flags(link.get(), IFF_UP);

    if (auto errLinkChange = rtnl_link_change(sock.get(), link.get(), link.get(), 0); errLinkChange < 0) {
        return NLToAosErr(errLinkChange, "failed to set link up");
    }

    return ErrorEnum::eNone;
}

Error InterfaceManager::AddLink(const LinkItf* link)
{
    const auto& attrs = link->GetAttrs();

    LOG_DBG() << "Add link: name=" << attrs.mName.c_str() << ", type=" << link->GetType();

    auto [sock, err] = CreateNetlinkSocket();
    if (!err.IsNone()) {
        return err;
    }

    auto [linkObj, linkErr] = CreateLink();
    if (!linkErr.IsNone()) {
        return linkErr;
    }

    rtnl_link_set_name(linkObj.get(), attrs.mName.c_str());

    if (attrs.mTxQLen >= 0) {
        rtnl_link_set_txqlen(linkObj.get(), attrs.mTxQLen);
    }

    rtnl_link_set_type(linkObj.get(), link->GetType());

    if (auto* vlan = dynamic_cast<const Vlan*>(link)) {
        rtnl_link_vlan_set_id(linkObj.get(), vlan->GetVlanId());
    }

    if (attrs.mParentIndex > 0) {
        auto [parent, parentErr] = CreateLink();
        if (!parentErr.IsNone()) {
            return parentErr;
        }

        rtnl_link_set_ifindex(parent.get(), attrs.mParentIndex);
        rtnl_link_set_link(linkObj.get(), attrs.mParentIndex);
    }

    if (!attrs.mMac.empty()) {
        struct nl_addr* macAddr = nullptr;

        if (nl_addr_parse(attrs.mMac.c_str(), AF_UNSPEC, &macAddr) < 0) {
            return NLToAosErr(-1, "failed to parse MAC address");
        }

        rtnl_link_set_addr(linkObj.get(), macAddr);
        nl_addr_put(macAddr);
    }

    if (auto errLinkAdd = rtnl_link_add(sock.get(), linkObj.get(), NLM_F_CREATE); errLinkAdd < 0) {
        return NLToAosErr(errLinkAdd, "failed to add link");
    }

    return ErrorEnum::eNone;
}

Error InterfaceManager::GetAddrList(const String& ifname, int family, Array<IPAddr>& addresses) const
{
    LOG_DBG() << "List addresses for interface: ifname=" << ifname;

    auto [sock, err] = CreateNetlinkSocket();
    if (!err.IsNone()) {
        return err;
    }

    nl_cache* cacheRaw;

    if (auto errAddrCache = rtnl_addr_alloc_cache(sock.get(), &cacheRaw); errAddrCache < 0) {
        return NLToAosErr(errAddrCache, "failed to allocate address cache");
    }

    [[maybe_unused]] auto cleanupCache = DeferRelease(cacheRaw, [](nl_cache* cache) { nl_cache_free(cache); });

    auto [link, linkErr] = CreateLink();
    if (!linkErr.IsNone()) {
        return linkErr;
    }

    rtnl_link_set_name(link.get(), ifname.CStr());

    for (auto obj = nl_cache_get_first(cacheRaw); obj != nullptr; obj = nl_cache_get_next(obj)) {
        auto addr = reinterpret_cast<struct rtnl_addr*>(obj);

        if (rtnl_addr_get_ifindex(addr) == rtnl_link_get_ifindex(link.get())
            && (family == AF_UNSPEC || rtnl_addr_get_family(addr) == family)) {

            IPAddr ipAddr;
            ipAddr.mFamily = rtnl_addr_get_family(addr);

            if (const auto* local = rtnl_addr_get_local(addr); local) {
                char buf[INET6_ADDRSTRLEN];

                nl_addr2str(local, buf, sizeof(buf));
                ipAddr.mIP = buf;
            }

            const char* label = rtnl_addr_get_label(addr);
            if (label) {
                ipAddr.mLabel = label;
            }

            if (err = addresses.PushBack(ipAddr); !err.IsNone()) {
                return AOS_ERROR_WRAP(err);
            }
        }
    }

    return ErrorEnum::eNone;
}

Error InterfaceManager::AddAddr(const String& ifname, const IPAddr& addr)
{
    LOG_DBG() << "Add address to interface: ifname=" << ifname.CStr() << ", IP=" << addr.mIP.c_str();

    auto [sock, err] = CreateNetlinkSocket();
    if (!err.IsNone()) {
        return err;
    }

    nl_cache* cacheRaw;

    if (auto errLinkCache = rtnl_link_alloc_cache(sock.get(), AF_UNSPEC, &cacheRaw); errLinkCache < 0) {
        return NLToAosErr(errLinkCache, "failed to allocate link cache");
    }

    [[maybe_unused]] auto cleanupCache = DeferRelease(cacheRaw, [](nl_cache* cache) { nl_cache_free(cache); });

    auto addrObj = DeferRelease(rtnl_addr_alloc(), rtnl_addr_put);
    if (!addrObj) {
        return NLToAosErr(errno, "failed to allocate address object");
    }

    auto link = DeferRelease(rtnl_link_get_by_name(cacheRaw, ifname.CStr()), rtnl_link_put);
    if (!link) {
        return NLToAosErr(errno, "failed to get interface");
    }

    int ifindex = rtnl_link_get_ifindex(link.Get());
    if (ifindex <= 0) {
        return NLToAosErr(errno, ("failed to get interface index for " + std::string(ifname.CStr())));
    }

    rtnl_addr_set_ifindex(addrObj.Get(), ifindex);

    struct nl_addr* local;

    if (auto errLocal = nl_addr_parse(addr.mIP.c_str(), addr.mFamily, &local); errLocal < 0) {
        return NLToAosErr(errLocal, ("failed to parse IP address " + std::string(addr.mIP)));
    }

    [[maybe_unused]] auto cleanupLocal = DeferRelease(local, [](nl_addr* addr) { nl_addr_put(addr); });

    rtnl_addr_set_local(addrObj.Get(), local);

    if (!addr.mSubnet.empty()) {
        struct nl_addr* subnet;

        if (auto errSubnet = nl_addr_parse(addr.mSubnet.c_str(), addr.mFamily, &subnet); errSubnet < 0) {
            return NLToAosErr(errSubnet, ("failed to parse subnet CIDR " + std::string(addr.mSubnet)));
        }

        [[maybe_unused]] auto cleanupSubnet = DeferRelease(subnet, [](nl_addr* addr) { nl_addr_put(addr); });
        int                   prefixlen     = nl_addr_get_prefixlen(subnet);

        rtnl_addr_set_prefixlen(addrObj.Get(), prefixlen);

        struct in_addr ipAddr, netmask, broadcast;
        inet_pton(AF_INET, addr.mIP.c_str(), &ipAddr);

        netmask.s_addr = htonl(~((1UL << (32 - prefixlen)) - 1));

        // Calculate broadcast: broadcast = ip | ~netmask
        broadcast.s_addr = (ipAddr.s_addr & netmask.s_addr) | ~netmask.s_addr;

        char brdStr[INET_ADDRSTRLEN];

        inet_ntop(AF_INET, &broadcast, brdStr, INET_ADDRSTRLEN);

        struct nl_addr* brd;

        if (nl_addr_parse(brdStr, addr.mFamily, &brd) >= 0) {
            [[maybe_unused]] auto cleanupBrd = DeferRelease(brd, [](nl_addr* addr) { nl_addr_put(addr); });
            rtnl_addr_set_broadcast(addrObj.Get(), brd);
        }
    }

    if (!addr.mLabel.empty()) {
        rtnl_addr_set_label(addrObj.Get(), addr.mLabel.c_str());
    }

    if (auto errAddrAdd = rtnl_addr_add(sock.get(), addrObj.Get(), 0); errAddrAdd < 0 && errAddrAdd != -NLE_EXIST) {
        return NLToAosErr(errAddrAdd, "failed to add address");
    }

    return ErrorEnum::eNone;
}

Error InterfaceManager::DeleteAddr(const String& ifname, const IPAddr& addr)
{
    LOG_DBG() << "Delete address from interface: ifname=" << ifname.CStr() << ", IP=" << addr.mIP.c_str();

    auto [sock, err] = CreateNetlinkSocket();
    if (!err.IsNone()) {
        return err;
    }

    auto addrObj = DeferRelease(rtnl_addr_alloc(), rtnl_addr_put);
    if (!addrObj) {
        return NLToAosErr(errno, "failed to allocate address object");
    }

    auto [link, linkErr] = CreateLink();
    if (!linkErr.IsNone()) {
        return linkErr;
    }

    rtnl_link_set_name(link.get(), ifname.CStr());
    rtnl_addr_set_ifindex(addrObj.Get(), rtnl_link_get_ifindex(link.get()));

    struct nl_addr* local;

    if (auto errLocal = nl_addr_parse(addr.mIP.c_str(), addr.mFamily, &local); errLocal < 0) {
        return Error(errLocal, ("failed to parse IP address " + std::string(addr.mIP)).c_str());
    }

    [[maybe_unused]] auto cleanupLocal = DeferRelease(local, [](nl_addr* addr) { nl_addr_put(addr); });

    rtnl_addr_set_local(addrObj.Get(), local);

    if (auto errAddrDelete = rtnl_addr_delete(sock.get(), addrObj.Get(), 0); errAddrDelete < 0) {
        return NLToAosErr(errAddrDelete, "failed to delete address");
    }

    return ErrorEnum::eNone;
}

Error InterfaceManager::SetMasterLink(const String& ifname, const String& master)
{
    LOG_DBG() << "Set master for interface: ifname=" << ifname << ", master=" << master;

    auto [sock, err] = CreateNetlinkSocket();
    if (!err.IsNone()) {
        return err;
    }

    nl_cache* cacheRaw;

    if (auto errLinkCache = rtnl_link_alloc_cache(sock.get(), AF_UNSPEC, &cacheRaw); errLinkCache < 0) {
        return NLToAosErr(errLinkCache, "failed to allocate link cache");
    }

    [[maybe_unused]] auto cleanupCache = DeferRelease(cacheRaw, [](nl_cache* cache) { nl_cache_free(cache); });

    auto masterLink = DeferRelease(rtnl_link_get_by_name(cacheRaw, master.CStr()), rtnl_link_put);
    if (!masterLink) {
        return NLToAosErr(errno, ("master interface not found  " + std::string(master.CStr())));
    }

    auto slaveLink = DeferRelease(rtnl_link_get_by_name(cacheRaw, ifname.CStr()), rtnl_link_put);
    if (!slaveLink) {
        return NLToAosErr(errno, ("slave interface not found " + std::string(ifname.CStr())));
    }

    auto change = DeferRelease(rtnl_link_alloc(), rtnl_link_put);
    if (!change) {
        return NLToAosErr(errno, "failed to allocate link change object");
    }

    int masterIndex = rtnl_link_get_ifindex(masterLink.Get());
    rtnl_link_set_master(change.Get(), masterIndex);

    if (auto errLinkChange = rtnl_link_change(sock.get(), slaveLink.Get(), change.Get(), 0); errLinkChange < 0) {
        return NLToAosErr(errLinkChange,
            ("failed to set master for " + std::string(ifname.CStr()) + " to bridge " + std::string(master.CStr())));
    }

    return ErrorEnum::eNone;
}

Error InterfaceManager::CreateBridge(const String& name, const String& ip, const String& subnet)
{
    LOG_DBG() << "Create bridge: name=" << name << ", ip=" << ip << ", subnet=" << subnet;

    LinkAttrs bridgeAttrs;
    bridgeAttrs.mName   = name.CStr();
    bridgeAttrs.mTxQLen = -1;

    Bridge bridge(bridgeAttrs);

    if (auto err = AddLink(&bridge); !err.IsNone()) {
        return err;
    }

    if (auto err = SetupLink(name); !err.IsNone()) {
        return err;
    }

    StaticArray<IPAddr, 1> addrs;

    if (auto err = GetAddrList(name, AF_INET, addrs); !err.IsNone()) {
        return err;
    }

    if (!addrs.IsEmpty()) {
        if (addrs.Size() > 1) {
            return Error(
                ErrorEnum::eFailed, ("bridge " + std::string(name.CStr()) + " has more than one address").c_str());
        }

        if (String(addrs[0].mIP.c_str()) == ip) {
            return ErrorEnum::eNone;
        }

        IPAddr ipAddr;
        ipAddr.mIP = addrs[0].mIP;

        if (auto err = DeleteAddr(name, ipAddr); !err.IsNone()) {
            return err;
        }
    }

    IPAddr ipAddr;
    ipAddr.mIP     = ip.CStr();
    ipAddr.mSubnet = subnet.CStr();

    if (auto err = AddAddr(name, ipAddr); !err.IsNone()) {
        return err;
    }

    return ErrorEnum::eNone;
}

Error InterfaceManager::CreateVlan(const String& name, uint64_t vlanId)
{
    if (mRandom == nullptr) {
        return Error(ErrorEnum::eFailed, "random generator is not initialized");
    }

    LOG_DBG() << "Create vlan: name=" << name << ", vlanId=" << vlanId;

    auto [masterIndex, err] = GetMasterInterfaceIndex();
    if (!err.IsNone()) {
        return err;
    }

    LinkAttrs vlanAttrs;
    vlanAttrs.mName        = name.CStr();
    vlanAttrs.mParentIndex = masterIndex;

    // cppcheck-suppress unusedScopedObject
    if (Tie(vlanAttrs.mMac, err) = GenerateMACAddress(*mRandom); !err.IsNone()) {
        return err;
    }

    Vlan vlan(vlanAttrs, vlanId);

    if (err = AddLink(&vlan); !err.IsNone()) {
        return err;
    }

    if (err = SetupLink(name); !err.IsNone()) {
        return err;
    }

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * Private InterfaceManager methods
 **********************************************************************************************************************/

RetWithError<int> InterfaceManager::GetMasterInterfaceIndex() const
{
    LOG_DBG() << "Get master interface index";

    StaticArray<RouteInfo, cMaxRouteCount> routes;

    if (auto err = GetRouteList(routes); !err.IsNone()) {
        return {-1, err};
    }

    auto it = std::find_if(
        routes.begin(), routes.end(), [](const RouteInfo& route) { return !route.mDestination.has_value(); });

    if (it != routes.end()) {
        return it->mLinkIndex;
    }

    return {-1, Error(ErrorEnum::eFailed, "no master interface found")};
}

RetWithError<InterfaceManager::UniqueLink> InterfaceManager::CreateLink() const
{
    auto link = UniqueLink(rtnl_link_alloc(), rtnl_link_put);
    if (!link) {
        return {nullptr, NLToAosErr(errno, "failed to allocate link object")};
    }

    return {std::move(link), ErrorEnum::eNone};
}

} // namespace aos::common::network
