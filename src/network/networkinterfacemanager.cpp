/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string>

#include <arpa/inet.h>
#include <errno.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include "logger/logmodule.hpp"
#include "network/networkinterfacemanager.hpp"

namespace aos::common::network {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error NetworkInterfaceManager::RemoveInterface(const String& ifname)
{
    LOG_DBG() << "Remove interface: ifname=" << ifname;

    auto sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (sock < 0) {
        return Error(errno, "failed to create netlink socket");
    }

    auto closeSock = DeferRelease(&sock, [](int* sock) { close(*sock); });

    struct {
        struct nlmsghdr  nlh;
        struct ifinfomsg ifi;
        // cppcheck-suppress unusedStructMember
        char buffer[256];
    } request;

    memset(&request, 0, sizeof(request));

    request.nlh.nlmsg_len   = NLMSG_LENGTH(sizeof(struct ifinfomsg));
    request.nlh.nlmsg_type  = RTM_DELLINK;
    request.nlh.nlmsg_flags = NLM_F_REQUEST;
    request.ifi.ifi_family  = AF_UNSPEC;

    auto rta = reinterpret_cast<struct rtattr*>(reinterpret_cast<char*>(&request) + NLMSG_ALIGN(request.nlh.nlmsg_len));

    rta->rta_type = IFLA_IFNAME;
    rta->rta_len  = RTA_LENGTH(ifname.Size() + 1);

    memcpy(RTA_DATA(rta), ifname.CStr(), ifname.Size() + 1);

    request.nlh.nlmsg_len += RTA_LENGTH(ifname.Size() + 1);

    if (send(sock, &request, request.nlh.nlmsg_len, 0) < 0) {
        return Error(errno, "failed to send netlink request");
    }

    return ErrorEnum::eNone;
}

Error NetworkInterfaceManager::BringUpInterface(const String& ifname)
{
    LOG_DBG() << "Bring up interface: ifname=" << ifname;

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        return Error(errno, "failed to create ioctl socket");
    }

    auto closeSock = DeferRelease(&sock, [](int* sock) { close(*sock); });

    struct ifreq ifr;

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname.CStr(), IFNAMSIZ - 1);

    if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) {
        return Error(errno, "failed to get interface flags");
    }

    ifr.ifr_flags |= IFF_UP | IFF_RUNNING;

    if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0) {
        return Error(errno, "failed to set interface flags");
    }

    return ErrorEnum::eNone;
}

} // namespace aos::common::network
