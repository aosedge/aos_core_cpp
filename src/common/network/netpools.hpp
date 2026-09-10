/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_NETWORK_NETPOOLS_HPP_
#define AOS_COMMON_NETWORK_NETPOOLS_HPP_

#include <array>

namespace aos::common::network {

/**
 * Container address pool shared by CM allocation and SM access control.
 */
struct NetworkPool {
    const char* mSubnet;
    int         mPrefix;
};

/**
 * Reserved AoS container networks, including networks allocated on other nodes.
 */
inline constexpr std::array<NetworkPool, 6> cNetworkPools {{{"172.17.0.0/16", 16}, {"172.18.0.0/16", 16},
    {"172.19.0.0/16", 16}, {"172.20.0.0/14", 16}, {"172.24.0.0/14", 16}, {"172.28.0.0/14", 16}}};

} // namespace aos::common::network

#endif
