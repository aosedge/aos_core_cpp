/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_NETWORK_UTILS_HPP_
#define AOS_COMMON_NETWORK_UTILS_HPP_

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include <netlink/addr.h>
#include <netlink/netlink.h>

#include <core/common/tools/array.hpp>
#include <core/sm/networkmanager/networkmanager.hpp>

namespace aos::common::network {

/**
 * Route info.
 */
struct RouteInfo {
    std::optional<std::string> mDestination;
    std::optional<std::string> mGateway;
    int                        mLinkIndex {};
};

using NetlinkSocketDeleter = std::function<void(nl_sock*)>;
using UniqueNetlinkSocket  = std::unique_ptr<nl_sock, NetlinkSocketDeleter>;

/**
 * Gets route list.
 *
 * @param[out] routes routes.
 * @return Error.
 */
Error GetRouteList(std::vector<RouteInfo>& routes);

/**
 * Creates netlink socket.
 *
 * @return netlink socket.
 */
RetWithError<UniqueNetlinkSocket> CreateNetlinkSocket();

/**
 * Checks if CIDR network overlaps with any route.
 *
 * @param toCheck CIDR network to check.
 * @param routes routes to check against.
 * @return true if overlaps, false otherwise.
 */
RetWithError<bool> CheckRouteOverlaps(const std::string& toCheck, const std::vector<RouteInfo>& routes);

/**
 * Parses address from CIDR.
 *
 * @param cidr CIDR to parse.
 * @return parsed address.
 */
RetWithError<nl_addr*> ParseAddress(const std::string& cidr);

/**
 * Converts netlink error to AOS exception.
 *
 * @param nlError netlink error.
 * @param message error message.
 */
void NLToAosException(int nlError, const std::string& message);

/**
 * Converts netlink error to AOS error.
 *
 * @param nlError netlink error.
 * @param message error message.
 * @return AOS error.
 */
Error NLToAosErr(int nlError, const std::string& message);

/**
 * Checks if network contains IP.
 *
 * @param networkCIDR network CIDR.
 * @param ipAddr IP address.
 * @return true if contains, false otherwise.
 */
bool NetworkContainsIP(const std::string& networkCIDR, const std::string& ipAddr);

} // namespace aos::common::network

#endif // AOS_COMMON_NETWORK_UTILS_HPP_
