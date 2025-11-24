/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_NETWORKMANAGER_ITF_DNSSERVER_HPP_
#define AOS_CM_NETWORKMANAGER_ITF_DNSSERVER_HPP_

#include <string>
#include <unordered_map>
#include <vector>

namespace aos::cm::networkmanager {

using HostsMap = std::unordered_map<std::string, std::vector<std::string>>;

/**
 * DNS server interface.
 */
class DNSServerItf {
public:
    /**
     * Destructor.
     */
    virtual ~DNSServerItf() = default;

    /**
     * Updates hosts file.
     *
     * @param hosts Hosts.
     * @return Error.
     */
    virtual Error UpdateHostsFile(const HostsMap& hosts) = 0;

    /**
     * Restarts DNS server.
     *
     * @return Error.
     */
    virtual Error Restart() = 0;

    /**
     * Gets IP.
     *
     * @return IP.
     */
    virtual std::string GetIP() const = 0;
};

} // namespace aos::cm::networkmanager

#endif // AOS_CM_NETWORKMANAGER_ITF_DNSSERVER_HPP_
