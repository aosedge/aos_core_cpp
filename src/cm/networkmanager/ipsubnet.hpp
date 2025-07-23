/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_NETWORKMANAGER_IPSUBNET_HPP_
#define AOS_CM_NETWORKMANAGER_IPSUBNET_HPP_

#include <deque>
#include <map>
#include <string>
#include <vector>

#include <common/network/interfacemanager.hpp>

#include "netpool.hpp"

namespace aos::cm::networkmanager {

/**
 * Subnet allocator.
 */
class IpSubnet {
public:
    /**
     * Default constructor.
     */
    IpSubnet() = default;

    /**
     * Initializes the subnet allocator.
     */
    void Init();

    /**
     * Gets an available IP from a subnet.
     *
     * @param networkID Network ID.
     * @return IP.
     */
    std::string GetAvailableIP(const std::string& networkID);

    /**
     * Gets an available subnet.
     *
     * @param networkID Network ID.
     * @return Subnet.
     */
    std::string GetAvailableSubnet(const std::string& networkID);

    /**
     * Releases an IP to a subnet.
     *
     * @param networkID Network ID.
     * @param ip IP to release.
     */
    void ReleaseIPToSubnet(const std::string& networkID, const std::string& ip);

    /**
     * Releases a subnet.
     *
     * @param networkID Network ID.
     */
    void ReleaseIPNetPool(const std::string& networkID);

    /**
     * Removes allocated subnets.
     *
     * @param networkID Network ID.
     * @param subnet Subnet.
     * @param IPs IPs.
     */
    void RemoveAllocatedSubnet(
        const std::string& networkID, const std::string& subnet, const std::vector<std::string>& IPs);

private:
    struct Subnetwork {
        std::string             mSubnet;
        std::deque<std::string> mIPs;
    };

    std::string RequestIPNetPool(const std::string& networkID);
    std::string FindUnusedIPSubnet();

    std::vector<std::string>          mPredefinedPrivateNetworks;
    std::map<std::string, Subnetwork> mUsedIPSubnets;
};

} // namespace aos::cm::networkmanager

#endif
