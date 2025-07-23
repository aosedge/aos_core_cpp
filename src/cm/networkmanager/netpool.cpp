/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <cstdint>
#include <map>
#include <stdexcept>

#include <arpa/inet.h>
#include <netlink/addr.h>
#include <netlink/netlink.h>

#include <common/network/utils.hpp>
#include <core/common/tools/memory.hpp>

#include "netpool.hpp"

namespace aos::cm::networkmanager {

namespace {

/***********************************************************************************************************************
 * Constants
 **********************************************************************************************************************/

const std::map<std::string, int> cNetPools = {
    {"172.17.0.0/16", 16},
    {"172.18.0.0/16", 16},
    {"172.19.0.0/16", 16},
    {"172.20.0.0/14", 16},
    {"172.24.0.0/14", 16},
    {"172.28.0.0/14", 16},
};

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

std::pair<uint32_t, int> ParseCIDRWithNetlink(const std::string& cidr)
{
    struct nl_addr* subnet;

    if (auto errSubnet = nl_addr_parse(cidr.c_str(), AF_INET, &subnet); errSubnet < 0) {
        common::network::NLToAosException(errSubnet, "failed to parse subnet CIDR " + cidr);
    }

    [[maybe_unused]] auto cleanupSubnet = DeferRelease(subnet, [](nl_addr* subnet) { nl_addr_put(subnet); });

    void* addrData = nl_addr_get_binary_addr(subnet);
    if (!addrData) {
        throw std::runtime_error("failed to get binary address from " + cidr);
    }

    uint32_t IP = ntohl(*reinterpret_cast<uint32_t*>(addrData));

    int prefix = nl_addr_get_prefixlen(subnet);

    return {IP, prefix};
}

std::string UINT32ToIP(uint32_t ip)
{
    struct in_addr addr;
    addr.s_addr = htonl(ip);

    return std::string(inet_ntoa(addr));
}

std::vector<std::string> MakeNetPool(int targetPrefix, uint32_t baseIP, int basePrefix)
{
    std::vector<std::string> result;
    uint32_t                 mask         = (0xFFFFFFFF << (32 - basePrefix));
    uint32_t                 maskedBaseIP = baseIP & mask;
    int                      subnetCount  = 1 << (targetPrefix - basePrefix);
    uint32_t                 subnetSize   = 1 << (32 - targetPrefix);

    for (int i = 0; i < subnetCount; i++) {
        uint32_t subnetIP = maskedBaseIP + (i * subnetSize);

        result.emplace_back(UINT32ToIP(subnetIP) + "/" + std::to_string(targetPrefix));
    }

    return result;
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

std::vector<std::string> GetNetPools()
{
    std::vector<std::string> pools;

    for (const auto& [pool, size] : cNetPools) {
        auto [baseIP, basePrefix] = ParseCIDRWithNetlink(pool);

        if (size <= 0 || size < basePrefix) {
            throw std::invalid_argument(
                "invalid pool size: " + std::to_string(size) + " for prefix " + std::to_string(basePrefix));
        }

        auto subnets = MakeNetPool(size, baseIP, basePrefix);

        pools.insert(pools.end(), subnets.begin(), subnets.end());
    }

    return pools;
}

std::deque<std::string> GenerateSubnetIPs(const std::string& cidr)
{
    std::deque<std::string> result;

    nl_addr* subnet;
    if (auto errSubnet = nl_addr_parse(cidr.c_str(), AF_INET, &subnet); errSubnet < 0) {
        common::network::NLToAosException(errSubnet, "failed to parse subnet CIDR " + cidr);
    }

    [[maybe_unused]] auto cleanupSubnet = DeferRelease(subnet, [](nl_addr* subnet) { nl_addr_put(subnet); });

    void* addrData = nl_addr_get_binary_addr(subnet);
    if (!addrData) {
        throw std::runtime_error("failed to get binary address from " + cidr);
    }

    uint32_t base      = ntohl(*reinterpret_cast<uint32_t*>(addrData));
    int      prefixLen = nl_addr_get_prefixlen(subnet);
    uint32_t mask      = prefixLen == 0 ? 0 : 0xFFFFFFFF << (32 - prefixLen);
    uint32_t network   = base & mask;
    uint32_t broadcast = network | (~mask);

    uint32_t hostBits = broadcast - network;
    if (hostBits <= 2) {
        throw std::runtime_error("invalid subnet CIDR: " + cidr);
    }

    uint32_t count = hostBits - 2;

    for (uint32_t i = 1; i <= count; ++i) {
        uint32_t ip = network + i + 1;
        if (ip >= broadcast) {
            break;
        }

        result.push_back(UINT32ToIP(ip));
    }

    return result;
}

} // namespace aos::cm::networkmanager
