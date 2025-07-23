/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_NETWORKMANAGER_NETPOOL_HPP_
#define AOS_CM_NETWORKMANAGER_NETPOOL_HPP_

#include <deque>
#include <string>
#include <vector>

namespace aos::cm::networkmanager {

/**
 * Returns network pools.
 *
 * @return Vector of network pools.
 */
std::vector<std::string> GetNetPools();

/**
 * Generates IP addresses for a subnet.
 *
 * @param cidr CIDR of the subnet.
 * @return Vector of IP addresses.
 */
std::deque<std::string> GenerateSubnetIPs(const std::string& cidr);

} // namespace aos::cm::networkmanager

#endif
