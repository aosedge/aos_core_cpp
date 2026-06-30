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
#include <fcntl.h>
#include <net/if.h>

#include <linux/if.h>
#include <linux/rtnetlink.h>
#include <netinet/in.h>
#include <netlink/errno.h>
#include <netlink/netlink.h>
#include <netlink/route/addr.h>
#include <netlink/route/link.h>
#include <netlink/route/link/bridge.h>
#include <netlink/route/link/veth.h>
#include <netlink/route/link/vlan.h>
#include <netlink/route/nexthop.h>
#include <netlink/route/route.h>
#include <sched.h>
#include <unistd.h>

#include <core/common/tools/logger.hpp>

#include "interfacemanager.hpp"

namespace aos::common::network {

namespace {

// Enters the given netns for the duration of fn(), then restores the original.
// Uses per-thread netns path (/proc/<pid>/task/<tid>/ns/net) so only the
// calling thread's netns is affected — safe in multi-threaded processes on
// Linux >= 3.8.
Error WithNetNS(const std::string& netNSPath, const std::function<Error()>& fn)
{
    char savedNsPath[64];
    snprintf(savedNsPath, sizeof(savedNsPath), "/proc/%d/task/%d/ns/net", getpid(), gettid());

    int savedNsFd = open(savedNsPath, O_RDONLY | O_CLOEXEC);
    if (savedNsFd < 0) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed, strerror(errno)));
    }

    [[maybe_unused]] auto closeSaved = DeferRelease(&savedNsFd, [](int* fd) { close(*fd); });

    int targetNsFd = open(netNSPath.c_str(), O_RDONLY | O_CLOEXEC);
    if (targetNsFd < 0) {
        return AOS_ERROR_WRAP(
            Error(ErrorEnum::eFailed, ("failed to open netns " + netNSPath + ": " + strerror(errno)).c_str()));
    }

    [[maybe_unused]] auto closeTarget = DeferRelease(&targetNsFd, [](int* fd) { close(*fd); });

    if (setns(targetNsFd, CLONE_NEWNET) != 0) {
        return AOS_ERROR_WRAP(
            Error(ErrorEnum::eFailed, ("failed to enter netns " + netNSPath + ": " + strerror(errno)).c_str()));
    }

    auto restoreNs = DeferRelease(&savedNsFd, [](int* fd) {
        if (setns(*fd, CLONE_NEWNET) != 0) {
            LOG_ERR() << "Failed to restore original netns: " << strerror(errno);
        }
    });

    return fn();
}

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

    auto [link, linkErr] = AllocLink();
    if (!linkErr.IsNone()) {
        return linkErr;
    }

    rtnl_link_set_name(link.get(), ifname.CStr());

    if (auto errLinkDel = rtnl_link_delete(sock.get(), link.get()); errLinkDel < 0) {
        if (errLinkDel == -NLE_OBJ_NOTFOUND || errLinkDel == -NLE_NODEV) {
            return Error(ErrorEnum::eNotFound, "link not found");
        }

        return NLToAosErr(errLinkDel, "failed to delete link");
    }

    return ErrorEnum::eNone;
}

Error InterfaceManager::SetupLink(const String& ifname, const String& netNSPath)
{
    LOG_DBG() << "Bring up interface: ifname=" << ifname;

    auto doSetupLink = [&]() -> Error {
        auto [sock, err] = CreateNetlinkSocket();
        if (!err.IsNone()) {
            return err;
        }

        auto [link, linkErr] = AllocLink();
        if (!linkErr.IsNone()) {
            return linkErr;
        }

        rtnl_link_set_name(link.get(), ifname.CStr());
        rtnl_link_set_flags(link.get(), IFF_UP);

        if (auto errLinkChange = rtnl_link_change(sock.get(), link.get(), link.get(), 0); errLinkChange < 0) {
            return NLToAosErr(errLinkChange, "failed to set link up");
        }

        return ErrorEnum::eNone;
    };

    if (netNSPath.IsEmpty()) {
        return doSetupLink();
    }

    return WithNetNS(std::string(netNSPath.CStr()), doSetupLink);
}

Error InterfaceManager::AddLink(const LinkItf* link)
{
    const auto& attrs = link->GetAttrs();

    LOG_DBG() << "Add link: name=" << attrs.mName.c_str() << ", type=" << link->GetType();

    auto [sock, err] = CreateNetlinkSocket();
    if (!err.IsNone()) {
        return err;
    }

    auto [linkObj, linkErr] = AllocLink();
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
        auto [parent, parentErr] = AllocLink();
        if (!parentErr.IsNone()) {
            return parentErr;
        }

        rtnl_link_set_ifindex(parent.get(), attrs.mParentIndex);
        rtnl_link_set_link(linkObj.get(), attrs.mParentIndex);
    }

    // Enslave to a master bridge in the same create message when requested.
    if (attrs.mMasterIndex > 0) {
        rtnl_link_set_master(linkObj.get(), attrs.mMasterIndex);
    }

    if (!attrs.mMac.empty()) {
        struct nl_addr* macAddr = nullptr;

        if (nl_addr_parse(attrs.mMac.c_str(), AF_UNSPEC, &macAddr) < 0) {
            return NLToAosErr(-1, "failed to parse MAC address");
        }

        rtnl_link_set_addr(linkObj.get(), macAddr);
        nl_addr_put(macAddr);
    }

    // Bring the link up as part of creation so callers don't need a separate
    // SetupLink round-trip.
    rtnl_link_set_flags(linkObj.get(), IFF_UP);

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

    auto [link, linkErr] = AllocLink();
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

    auto addrObj = DeferRelease(rtnl_addr_alloc(), rtnl_addr_put);
    if (!addrObj) {
        return NLToAosErr(errno, "failed to allocate address object");
    }

    // Resolve the ifindex via if_nametoindex (ioctl) instead of dumping the link table.
    unsigned int ifindexU = if_nametoindex(ifname.CStr());
    if (ifindexU == 0) {
        return AOS_ERROR_WRAP(
            Error(ErrorEnum::eNotFound, ("failed to get interface " + std::string(ifname.CStr())).c_str()));
    }

    int ifindex = static_cast<int>(ifindexU);

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

    auto [link, linkErr] = AllocLink();
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

    // Resolve both names via if_nametoindex (a cheap ioctl) instead of dumping
    // the whole link table just to find two ifindexes — saves a netlink
    // round-trip, which matters most under CPU contention.
    unsigned int masterIndex = if_nametoindex(master.CStr());
    if (masterIndex == 0) {
        return AOS_ERROR_WRAP(
            Error(ErrorEnum::eNotFound, ("master interface not found " + std::string(master.CStr())).c_str()));
    }

    unsigned int slaveIndex = if_nametoindex(ifname.CStr());
    if (slaveIndex == 0) {
        return AOS_ERROR_WRAP(
            Error(ErrorEnum::eNotFound, ("slave interface not found " + std::string(ifname.CStr())).c_str()));
    }

    auto orig = DeferRelease(rtnl_link_alloc(), rtnl_link_put);
    if (!orig) {
        return NLToAosErr(errno, "failed to allocate link object");
    }

    rtnl_link_set_ifindex(orig.Get(), static_cast<int>(slaveIndex));

    auto change = DeferRelease(rtnl_link_alloc(), rtnl_link_put);
    if (!change) {
        return NLToAosErr(errno, "failed to allocate link change object");
    }

    rtnl_link_set_master(change.Get(), static_cast<int>(masterIndex));

    if (auto errLinkChange = rtnl_link_change(sock.get(), orig.Get(), change.Get(), 0); errLinkChange < 0) {
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

    // AddLink brings the bridge up as part of creation.
    if (auto err = AddLink(&bridge); !err.IsNone()) {
        return err;
    }

    IPAddr ipAddr;
    ipAddr.mIP     = ip.CStr();
    ipAddr.mSubnet = subnet.CStr();

    // AddAddr is idempotent (ignores EEXIST), so no need to probe/clear the
    // existing address first.
    if (auto err = AddAddr(name, ipAddr); !err.IsNone()) {
        return err;
    }

    return ErrorEnum::eNone;
}

Error InterfaceManager::CreateVlan(const String& name, uint64_t vlanId, const String& master)
{
    if (mRandom == nullptr) {
        return Error(ErrorEnum::eFailed, "random generator is not initialized");
    }

    LOG_DBG() << "Create vlan: name=" << name << ", vlanId=" << vlanId << ", master=" << master;

    auto [parentIndex, err] = GetMasterInterfaceIndex();
    if (!err.IsNone()) {
        return err;
    }

    LinkAttrs vlanAttrs;
    vlanAttrs.mName        = name.CStr();
    vlanAttrs.mParentIndex = parentIndex;

    // Enslave to the bridge in the same create message (avoids a separate
    // SetMasterLink round-trip).
    if (!master.IsEmpty()) {
        unsigned int masterIndex = if_nametoindex(master.CStr());
        if (masterIndex == 0) {
            return AOS_ERROR_WRAP(
                Error(ErrorEnum::eNotFound, ("master interface not found " + std::string(master.CStr())).c_str()));
        }

        vlanAttrs.mMasterIndex = static_cast<int>(masterIndex);
    }

    // cppcheck-suppress unusedScopedObject
    if (Tie(vlanAttrs.mMac, err) = GenerateMACAddress(*mRandom); !err.IsNone()) {
        return err;
    }

    Vlan vlan(vlanAttrs, vlanId);

    // AddLink brings the vlan up as part of creation.
    if (err = AddLink(&vlan); !err.IsNone()) {
        return err;
    }

    return ErrorEnum::eNone;
}

Error InterfaceManager::CreateLink(const String& name, const String& kind)
{
    LOG_DBG() << "Create link" << Log::Field("name", name) << Log::Field("kind", kind);

    auto [sock, err] = CreateNetlinkSocket();
    if (!err.IsNone()) {
        return err;
    }

    auto [link, linkErr] = AllocLink();
    if (!linkErr.IsNone()) {
        return linkErr;
    }

    rtnl_link_set_name(link.get(), name.CStr());

    if (auto errType = rtnl_link_set_type(link.get(), kind.CStr()); errType < 0) {
        return NLToAosErr(errType, "failed to set link type");
    }

    if (auto errAdd = rtnl_link_add(sock.get(), link.get(), NLM_F_CREATE); errAdd < 0) {
        return NLToAosErr(errAdd, "failed to add link");
    }

    return ErrorEnum::eNone;
}

Error InterfaceManager::CreateVeth(const String& hostIfName, const String& peerIfName)
{
    LOG_DBG() << "Create veth pair" << Log::Field("host", hostIfName) << Log::Field("peer", peerIfName);

    auto host = DeferRelease(rtnl_link_veth_alloc(), rtnl_link_put);
    if (!host) {
        return NLToAosErr(errno, "failed to allocate veth link");
    }

    rtnl_link_set_name(host.Get(), hostIfName.CStr());

    // rtnl_link_veth_get_peer is owned by the host link — no separate free.
    auto* peer = rtnl_link_veth_get_peer(host.Get());
    rtnl_link_set_name(peer, peerIfName.CStr());

    auto [sock, err] = CreateNetlinkSocket();
    if (!err.IsNone()) {
        return err;
    }

    if (auto errAdd = rtnl_link_add(sock.get(), host.Get(), NLM_F_CREATE); errAdd < 0) {
        return NLToAosErr(errAdd, "failed to create veth pair");
    }

    return ErrorEnum::eNone;
}

Error InterfaceManager::CreateVethToNamespace(
    const String& hostIfName, const String& peerIfName, const String& netNSPath, const String& master)
{
    LOG_DBG() << "Create veth to namespace" << Log::Field("host", hostIfName) << Log::Field("peer", peerIfName)
              << Log::Field("netNSPath", netNSPath) << Log::Field("master", master);

    int nsFd = open(netNSPath.CStr(), O_RDONLY | O_CLOEXEC);
    if (nsFd < 0) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed,
            ("failed to open netns " + std::string(netNSPath.CStr()) + ": " + strerror(errno)).c_str()));
    }

    [[maybe_unused]] auto closeNsFd = DeferRelease(&nsFd, [](int* fd) { close(*fd); });

    auto host = DeferRelease(rtnl_link_veth_alloc(), rtnl_link_put);
    if (!host) {
        return NLToAosErr(errno, "failed to allocate veth link");
    }

    rtnl_link_set_name(host.Get(), hostIfName.CStr());

    // Bring the host side up as part of creation — saves a SetupLink round-trip.
    // (The peer is brought up later inside its namespace by ConfigureInstanceInterface.)
    rtnl_link_set_flags(host.Get(), IFF_UP);

    // Enslave the host side to the bridge in the same create message (avoids a
    // separate SetMasterLink round-trip).
    if (!master.IsEmpty()) {
        unsigned int masterIndex = if_nametoindex(master.CStr());
        if (masterIndex == 0) {
            return AOS_ERROR_WRAP(
                Error(ErrorEnum::eNotFound, ("master interface not found " + std::string(master.CStr())).c_str()));
        }

        rtnl_link_set_master(host.Get(), static_cast<int>(masterIndex));
    }

    // rtnl_link_veth_get_peer is owned by the host link — no separate free.
    // Setting the peer's netns fd makes the kernel create it directly in the
    // target namespace, already carrying peerIfName — no separate move/rename.
    auto* peer = rtnl_link_veth_get_peer(host.Get());

    rtnl_link_set_name(peer, peerIfName.CStr());
    rtnl_link_set_ns_fd(peer, nsFd);

    auto [sock, err] = CreateNetlinkSocket();
    if (!err.IsNone()) {
        return err;
    }

    if (auto errAdd = rtnl_link_add(sock.get(), host.Get(), NLM_F_CREATE); errAdd < 0) {
        return NLToAosErr(errAdd, "failed to create veth pair into namespace");
    }

    return ErrorEnum::eNone;
}

Error InterfaceManager::ConfigureInstanceInterface(
    const String& ifname, const String& ipWithMask, const String& gateway, const String& netNSPath)
{
    LOG_DBG() << "Configure instance interface" << Log::Field("ifname", ifname) << Log::Field("ipWithMask", ipWithMask)
              << Log::Field("gateway", gateway);

    std::string cidr {ipWithMask.CStr()};
    auto        slashPos = cidr.find('/');

    if (slashPos == std::string::npos) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eInvalidArgument, "expected CIDR form ip/mask not found"));
    }

    IPAddr addr;
    addr.mIP     = cidr.substr(0, slashPos);
    addr.mSubnet = cidr;
    addr.mFamily = AF_INET;

    // All three steps run inside the instance netns in a single namespace entry
    // (one setns pair) instead of one per operation. Each callee is invoked with
    // an empty netNSPath so it operates in the already-entered namespace.
    auto doConfigure = [&]() -> Error {
        if (auto err = SetupLink(ifname); !err.IsNone()) {
            return err;
        }

        if (auto err = AddAddr(ifname, addr); !err.IsNone()) {
            return err;
        }

        if (auto err = AddRoute("0.0.0.0/0", gateway, ""); !err.IsNone()) {
            return err;
        }

        return ErrorEnum::eNone;
    };

    return WithNetNS(std::string(netNSPath.CStr()), doConfigure);
}

Error InterfaceManager::MoveLinkToNamespace(const String& ifname, const String& netNSPath)
{
    LOG_DBG() << "Move link to namespace" << Log::Field("ifname", ifname) << Log::Field("netNSPath", netNSPath);

    int nsFd = open(netNSPath.CStr(), O_RDONLY | O_CLOEXEC);
    if (nsFd < 0) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed,
            ("failed to open netns " + std::string(netNSPath.CStr()) + ": " + strerror(errno)).c_str()));
    }

    [[maybe_unused]] auto closeNsFd = DeferRelease(&nsFd, [](int* fd) { close(*fd); });

    auto [sock, err] = CreateNetlinkSocket();
    if (!err.IsNone()) {
        return err;
    }

    nl_cache* cacheRaw;

    if (auto errCache = rtnl_link_alloc_cache(sock.get(), AF_UNSPEC, &cacheRaw); errCache < 0) {
        return NLToAosErr(errCache, "failed to allocate link cache");
    }

    [[maybe_unused]] auto cleanupCache = DeferRelease(cacheRaw, [](nl_cache* cache) { nl_cache_free(cache); });

    auto link = DeferRelease(rtnl_link_get_by_name(cacheRaw, ifname.CStr()), rtnl_link_put);
    if (!link) {
        return AOS_ERROR_WRAP(
            Error(ErrorEnum::eFailed, ("interface not found: " + std::string(ifname.CStr())).c_str()));
    }

    auto change = DeferRelease(rtnl_link_alloc(), rtnl_link_put);
    if (!change) {
        return NLToAosErr(errno, "failed to allocate link change object");
    }

    rtnl_link_set_ns_fd(change.Get(), nsFd);

    if (auto errChange = rtnl_link_change(sock.get(), link.Get(), change.Get(), 0); errChange < 0) {
        return NLToAosErr(errChange, "failed to move link to namespace");
    }

    return ErrorEnum::eNone;
}

Error InterfaceManager::RenameLink(const String& ifname, const String& newName, const String& netNSPath)
{
    LOG_DBG() << "Rename link" << Log::Field("ifname", ifname) << Log::Field("newName", newName);

    auto doRename = [&]() -> Error {
        auto [sock, err] = CreateNetlinkSocket();
        if (!err.IsNone()) {
            return err;
        }

        nl_cache* cacheRaw;

        if (auto errCache = rtnl_link_alloc_cache(sock.get(), AF_UNSPEC, &cacheRaw); errCache < 0) {
            return NLToAosErr(errCache, "failed to allocate link cache");
        }

        [[maybe_unused]] auto cleanupCache = DeferRelease(cacheRaw, [](nl_cache* cache) { nl_cache_free(cache); });

        auto link = DeferRelease(rtnl_link_get_by_name(cacheRaw, ifname.CStr()), rtnl_link_put);
        if (!link) {
            return AOS_ERROR_WRAP(
                Error(ErrorEnum::eFailed, ("interface not found: " + std::string(ifname.CStr())).c_str()));
        }

        auto change = DeferRelease(rtnl_link_alloc(), rtnl_link_put);
        if (!change) {
            return NLToAosErr(errno, "failed to allocate link change object");
        }

        rtnl_link_set_name(change.Get(), newName.CStr());

        if (auto errChange = rtnl_link_change(sock.get(), link.Get(), change.Get(), 0); errChange < 0) {
            return NLToAosErr(errChange, "failed to rename link");
        }

        return ErrorEnum::eNone;
    };

    if (netNSPath.IsEmpty()) {
        return doRename();
    }

    return WithNetNS(std::string(netNSPath.CStr()), doRename);
}

Error InterfaceManager::AddAddress(const String& ifname, const String& ipWithMask, const String& netNSPath)
{
    LOG_DBG() << "Add address" << Log::Field("ifname", ifname) << Log::Field("ipWithMask", ipWithMask);

    std::string cidr(ipWithMask.CStr());
    auto        slashPos = cidr.find('/');

    if (slashPos == std::string::npos) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eInvalidArgument, "expected CIDR form ip/mask not found"));
    }

    IPAddr addr;
    addr.mIP     = cidr.substr(0, slashPos);
    addr.mSubnet = cidr;
    addr.mFamily = AF_INET;

    if (netNSPath.IsEmpty()) {
        return AddAddr(ifname, addr);
    }

    return WithNetNS(std::string(netNSPath.CStr()), [this, &ifname, &addr]() { return AddAddr(ifname, addr); });
}

Error InterfaceManager::AddRoute(const String& destination, const String& gateway, const String& netNSPath)
{
    LOG_DBG() << "Add route" << Log::Field("dst", destination) << Log::Field("gw", gateway);

    auto doAddRoute = [&]() -> Error {
        auto [sock, err] = CreateNetlinkSocket();
        if (!err.IsNone()) {
            return err;
        }

        auto route = DeferRelease(rtnl_route_alloc(), rtnl_route_put);
        if (!route) {
            return NLToAosErr(errno, "failed to allocate route");
        }

        rtnl_route_set_scope(route.Get(), RT_SCOPE_UNIVERSE);
        rtnl_route_set_table(route.Get(), RT_TABLE_MAIN);
        rtnl_route_set_protocol(route.Get(), RTPROT_STATIC);

        struct nl_addr* dst = nullptr;

        if (nl_addr_parse(destination.CStr(), AF_INET, &dst) < 0) {
            return AOS_ERROR_WRAP(
                Error(ErrorEnum::eFailed, ("failed to parse destination: " + std::string(destination.CStr())).c_str()));
        }

        [[maybe_unused]] auto cleanupDst = DeferRelease(dst, [](nl_addr* a) { nl_addr_put(a); });

        rtnl_route_set_dst(route.Get(), dst);

        auto* nh = rtnl_route_nh_alloc();
        if (!nh) {
            return NLToAosErr(errno, "failed to allocate nexthop");
        }

        struct nl_addr* gw = nullptr;

        if (nl_addr_parse(gateway.CStr(), AF_INET, &gw) < 0) {
            rtnl_route_nh_free(nh);

            return AOS_ERROR_WRAP(
                Error(ErrorEnum::eFailed, ("failed to parse gateway: " + std::string(gateway.CStr())).c_str()));
        }

        [[maybe_unused]] auto cleanupGw = DeferRelease(gw, [](nl_addr* a) { nl_addr_put(a); });

        rtnl_route_nh_set_gateway(nh, gw);
        rtnl_route_add_nexthop(route.Get(), nh);
        // rtnl_route_add_nexthop takes ownership of nh — no separate free.

        if (auto errAdd = rtnl_route_add(sock.get(), route.Get(), NLM_F_CREATE); errAdd < 0) {
            return NLToAosErr(errAdd, "failed to add route");
        }

        return ErrorEnum::eNone;
    };

    if (netNSPath.IsEmpty()) {
        return doAddRoute();
    }

    return WithNetNS(std::string(netNSPath.CStr()), doAddRoute);
}

Error InterfaceManager::SetHairpin(const String& ifname, bool enable)
{
    LOG_DBG() << "Set hairpin" << Log::Field("ifname", ifname) << Log::Field("enable", enable);

    // sysfs write is simpler and more portable than the libnl bridge-port API.
    std::string sysfsPath = std::string("/sys/class/net/") + ifname.CStr() + "/brport/hairpin_mode";

    int fd = open(sysfsPath.c_str(), O_WRONLY);
    if (fd < 0) {
        return AOS_ERROR_WRAP(
            Error(ErrorEnum::eFailed, ("failed to open " + sysfsPath + ": " + strerror(errno)).c_str()));
    }

    [[maybe_unused]] auto closeFd = DeferRelease(&fd, [](int* f) { close(*f); });

    const char value = enable ? '1' : '0';

    if (write(fd, &value, 1) != 1) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eFailed,
            ("failed to write hairpin_mode for " + std::string(ifname.CStr()) + ": " + strerror(errno)).c_str()));
    }

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * Private InterfaceManager methods
 **********************************************************************************************************************/

RetWithError<int> InterfaceManager::GetMasterInterfaceIndex() const
{
    LOG_DBG() << "Get master interface index";

    std::vector<RouteInfo> routes;

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

RetWithError<InterfaceManager::UniqueLink> InterfaceManager::AllocLink() const
{
    auto link = UniqueLink(rtnl_link_alloc(), rtnl_link_put);
    if (!link) {
        return {nullptr, NLToAosErr(errno, "failed to allocate link object")};
    }

    return {std::move(link), ErrorEnum::eNone};
}

} // namespace aos::common::network
