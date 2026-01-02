/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <exception>

#include <netlink/netlink.h>
#include <netlink/route/addr.h>
#include <netlink/route/route.h>

#include <common/utils/exception.hpp>

#include "utils.hpp"

namespace aos::common::network {

namespace {

std::string ExtractIPFromCIDR(const std::string& cidr)
{
    size_t slashPos = cidr.find('/');

    return slashPos != std::string::npos ? cidr.substr(0, slashPos) : cidr;
}

} // namespace

void NLToAosException(int nlError, const std::string& message)
{
    auto errMsg = message + ": " + nl_geterror(nlError);
    throw common::utils::AosException(Error(ErrorEnum::eFailed, errMsg.c_str()));
}

Error NLToAosErr(int nlError, const std::string& message)
{
    return Error(ErrorEnum::eFailed, (message + ": " + std::string(nl_geterror(nlError))).c_str());
}

bool NetworkContainsIP(const std::string& networkCIDR, const std::string& ipAddr)
{
    auto [network, errNetwork] = ParseAddress(networkCIDR);
    AOS_ERROR_CHECK_AND_THROW(errNetwork, "failed to parse network");

    [[maybe_unused]] auto cleanupNetwork = DeferRelease(network, [](nl_addr* addr) { nl_addr_put(addr); });

    auto [ip, errIp] = ParseAddress(ipAddr + "/32");
    AOS_ERROR_CHECK_AND_THROW(errIp, "failed to parse IP");

    [[maybe_unused]] auto cleanupIp = DeferRelease(ip, [](nl_addr* addr) { nl_addr_put(addr); });

    void* networkData = nl_addr_get_binary_addr(network);
    void* ipData      = nl_addr_get_binary_addr(ip);

    if (!networkData || !ipData) {
        throw std::runtime_error("failed to get binary address");
    }

    uint32_t networkAddr = ntohl(*reinterpret_cast<uint32_t*>(networkData));
    uint32_t ipAddress   = ntohl(*reinterpret_cast<uint32_t*>(ipData));
    int      prefixLen   = nl_addr_get_prefixlen(network);

    uint32_t mask = (0xFFFFFFFF << (32 - prefixLen));

    bool result = (networkAddr & mask) == (ipAddress & mask);

    return result;
}

Error GetRouteList(std::vector<RouteInfo>& routes)
{
    auto [sock, err] = CreateNetlinkSocket();
    if (!err.IsNone()) {
        return err;
    }

    nl_cache* cacheRaw;

    if (auto errRouteCache = rtnl_route_alloc_cache(sock.get(), AF_INET, 0, &cacheRaw); errRouteCache < 0) {
        return NLToAosErr(errRouteCache, "failed to allocate route cache");
    }

    [[maybe_unused]] auto cleanupCache = DeferRelease(cacheRaw, [](nl_cache* cache) { nl_cache_free(cache); });

    for (auto obj = nl_cache_get_first(cacheRaw); obj != nullptr; obj = nl_cache_get_next(obj)) {
        auto route = reinterpret_cast<struct rtnl_route*>(obj);

        auto* nh = rtnl_route_nexthop_n(route, 0);
        if (!nh) {
            continue;
        }

        RouteInfo info;

        info.mLinkIndex = rtnl_route_nh_get_ifindex(nh);

        if (rtnl_route_get_table(route) == RT_TABLE_MAIN) {
            if (const auto* dst = rtnl_route_get_dst(route); dst && nl_addr_get_prefixlen(dst) > 0) {
                char buf[INET6_ADDRSTRLEN];

                nl_addr2str(dst, buf, sizeof(buf));
                info.mDestination = buf;
            }

            // Get gateway from next hop
            if (const auto* gw = rtnl_route_nh_get_gateway(nh); gw) {
                char buf[INET6_ADDRSTRLEN];

                nl_addr2str(gw, buf, sizeof(buf));
                info.mGateway = buf;
            }
        }

        routes.push_back(info);
    }

    return ErrorEnum::eNone;
}

RetWithError<UniqueNetlinkSocket> CreateNetlinkSocket()
{
    auto sock = UniqueNetlinkSocket(nl_socket_alloc(), nl_socket_free);
    if (!sock) {
        return {nullptr, NLToAosErr(errno, "failed to allocate netlink socket")};
    }

    if (nl_connect(sock.get(), NETLINK_ROUTE) < 0) {
        return {nullptr, NLToAosErr(errno, "failed to connect to netlink")};
    }

    return {std::move(sock), ErrorEnum::eNone};
}

RetWithError<nl_addr*> ParseAddress(const std::string& cidr)
{
    nl_addr* addr;

    if (auto err = nl_addr_parse(cidr.c_str(), AF_INET, &addr); err < 0) {
        return {nullptr, NLToAosErr(err, "failed to parse " + cidr)};
    }

    return addr;
}

RetWithError<bool> CheckRouteOverlaps(const std::string& toCheck, const std::vector<RouteInfo>& routes)
{
    try {
        for (const auto& route : routes) {
            if (route.mGateway.has_value() && NetworkContainsIP(toCheck, route.mGateway.value())) {
                return {true, ErrorEnum::eNone};
            }

            if (!route.mDestination.has_value()) {
                continue;
            }

            std::string routeIP = ExtractIPFromCIDR(route.mDestination.value());
            std::string checkIP = ExtractIPFromCIDR(toCheck);

            if (NetworkContainsIP(toCheck, routeIP) || NetworkContainsIP(route.mDestination.value(), checkIP)) {
                return {true, ErrorEnum::eNone};
            }
        }
    } catch (const std::exception& e) {
        return {false, Error(ErrorEnum::eFailed, e.what())};
    }

    return {false, ErrorEnum::eNone};
}

} // namespace aos::common::network
