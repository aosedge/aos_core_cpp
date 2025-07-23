/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <algorithm>
#include <stdexcept>

#include <common/utils/exception.hpp>
#include <core/common/tools/array.hpp>

#include "ipsubnet.hpp"

namespace aos::cm::networkmanager {

/**********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

void IpSubnet::Init()
{
    mPredefinedPrivateNetworks = GetNetPools();
}

std::string IpSubnet::GetAvailableSubnet(const std::string& networkID)
{
    auto it = mUsedIPSubnets.find(networkID);
    if (it == mUsedIPSubnets.end()) {
        return RequestIPNetPool(networkID);
    }

    return it->second.mSubnet;
}

std::string IpSubnet::GetAvailableIP(const std::string& networkID)
{
    auto it = mUsedIPSubnets.find(networkID);
    if (it == mUsedIPSubnets.end() || it->second.mIPs.empty()) {
        throw std::runtime_error("no available IP for network " + networkID);
    }

    std::string ip = it->second.mIPs.front();
    it->second.mIPs.pop_front();

    return ip;
}

void IpSubnet::ReleaseIPToSubnet(const std::string& networkID, const std::string& ip)
{
    auto it = mUsedIPSubnets.find(networkID);
    if (it == mUsedIPSubnets.end()) {
        return;
    }

    it->second.mIPs.push_back(ip);
}

void IpSubnet::ReleaseIPNetPool(const std::string& networkID)
{
    auto it = mUsedIPSubnets.find(networkID);
    if (it == mUsedIPSubnets.end()) {
        return;
    }

    mPredefinedPrivateNetworks.push_back(it->second.mSubnet);
    mUsedIPSubnets.erase(it);
}

void IpSubnet::RemoveAllocatedSubnet(
    const std::string& networkID, const std::string& subnet, const std::vector<std::string>& IPs)
{
    if (auto it = std::find(mPredefinedPrivateNetworks.begin(), mPredefinedPrivateNetworks.end(), subnet);
        it != mPredefinedPrivateNetworks.end()) {
        mUsedIPSubnets[networkID] = Subnetwork {*it, GenerateSubnetIPs(*it)};
        mPredefinedPrivateNetworks.erase(it);
    }

    for (auto it = IPs.begin(); it != IPs.end(); ++it) {
        auto it2 = mUsedIPSubnets.find(networkID);
        if (it2 == mUsedIPSubnets.end()) {
            continue;
        }

        auto& ips = it2->second.mIPs;
        ips.erase(std::remove(ips.begin(), ips.end(), *it), ips.end());
    }
}

/**********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

std::string IpSubnet::RequestIPNetPool(const std::string& networkID)
{
    if (mPredefinedPrivateNetworks.empty()) {
        throw std::runtime_error("no available subnet for network " + networkID);
    }

    auto cidr = FindUnusedIPSubnet();

    mUsedIPSubnets[networkID] = Subnetwork {cidr, GenerateSubnetIPs(cidr)};

    return cidr;
}

std::string IpSubnet::FindUnusedIPSubnet()
{
    StaticArray<common::network::RouteInfo, common::network::cMaxRouteCount> routes;

    auto err = common::network::GetRouteList(routes);
    AOS_ERROR_CHECK_AND_THROW(err, "failed to get routes");

    for (auto it = mPredefinedPrivateNetworks.begin(); it != mPredefinedPrivateNetworks.end(); ++it) {
        auto [overlaps, errOverlaps] = common::network::CheckRouteOverlaps(*it, routes);
        AOS_ERROR_CHECK_AND_THROW(errOverlaps, "failed to check route overlaps");

        if (!overlaps) {
            auto cidr = *it;
            mPredefinedPrivateNetworks.erase(it);

            return cidr;
        }
    }

    throw std::runtime_error("no available network");
}

} // namespace aos::cm::networkmanager
