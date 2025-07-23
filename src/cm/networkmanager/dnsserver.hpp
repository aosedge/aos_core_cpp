/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_NETWORKMANAGER_DNSSERVER_HPP_
#define AOS_CM_NETWORKMANAGER_DNSSERVER_HPP_

#include <string>
#include <vector>

#include <Poco/Process.h>

#include <core/common/tools/error.hpp>

#include "itf/dnsserver.hpp"

namespace aos::cm::networkmanager {

/**
 * DNS server.
 */
class DNSServer : public DNSServerItf {
public:
    /**
     * Constructor.
     */
    DNSServer() = default;

    /**
     * Initializes DNS server.
     *
     * @param dnsStoragePath DNS storage path.
     * @param IP IP.
     */
    void Init(const std::string& dnsStoragePath, const std::string& IP);

    /**
     * Updates hosts file.
     *
     * @param hosts Hosts.
     * @return Error.
     */
    Error UpdateHostsFile(const HostsMap& hosts) override;

    /**
     * Restarts DNS server.
     *
     * @return Error.
     */
    Error Restart() override;

    /**
     * Returns IP.
     *
     * @return IP.
     */
    std::string GetIP() const override;

private:
    static constexpr auto cHostFileName = "addnhosts";
    static constexpr auto cPidFileName  = "pidfile";

    Poco::Process::PID FindServerProcess();
    void               RestartProcess(Poco::Process::PID pid);

    std::string mDnsStoragePath;
    std::string mIP;
};

} // namespace aos::cm::networkmanager

#endif // AOS_CM_NETWORKMANAGER_DNSSERVER_HPP_
