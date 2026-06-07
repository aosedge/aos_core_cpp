/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_NETWORKMANAGER_DNSSERVER_HPP_
#define AOS_SM_NETWORKMANAGER_DNSSERVER_HPP_

#include <map>
#include <string>
#include <vector>

#include <Poco/Process.h>

#include <core/common/tools/noncopyable.hpp>
#include <core/sm/networkmanager/itf/dnsname.hpp>

#include <common/process/itf/processspawner.hpp>

namespace aos::sm::networkmanager {

/**
 * Per-network DNS server (DNSServerItf).
 *
 * One DNSServer is created per bridge/network by DNSName and corresponds to
 * one dnsmasq process. It owns the instanceID -> {IP, names} record map,
 * rewrites <storageDir>/addnhosts on every AddHost / RemoveHost, and signals
 * dnsmasq with SIGHUP via the process spawner so the new addnhosts is picked
 * up without restarting the process.
 *
 * dnsmasq lifecycle (spawn/kill, pidfile reading) is owned by the DNSName
 * factory; this handle only edits the hosts file and signals the running
 * process.
 */
class DNSServer : public DNSServerItf, private NonCopyable {
public:
    /**
     * Initializes the handle. Truncates the addnhosts file and signals dnsmasq
     * so any stale entries (e.g. inherited from a previous SM lifetime) are
     * dropped before any AddHost/RemoveHost runs.
     *
     * @param networkID network id (acts as the DNS domain).
     * @param storageDir per-network directory containing addnhosts and pidfile.
     * @param spawner process spawner used for SIGHUP.
     * @param pid dnsmasq PID.
     * @return Error.
     */
    Error Init(const std::string& networkID, const std::string& storageDir,
        aos::common::process::ProcessSpawnerItf& spawner, Poco::Process::PID pid);

    /**
     * Adds (or replaces) the hosts entry for an instance.
     *
     * @param instanceID instance id.
     * @param params instance IP and aliases.
     * @return Error.
     */
    Error AddHost(const String& instanceID, const DNSAliasesParams& params) override;

    /**
     * Removes the hosts entry for an instance. Returns eNone when the
     * instance is unknown.
     *
     * @param instanceID instance id.
     * @return Error.
     */
    Error RemoveHost(const String& instanceID) override;

private:
    struct HostRecord {
        std::string              mIP;
        std::vector<std::string> mNames;
    };

    Error WriteHostsFile() const;
    Error Reload() const;

    std::string                              mNetworkID;
    std::string                              mStorageDir;
    aos::common::process::ProcessSpawnerItf* mSpawner {};
    Poco::Process::PID                       mPID {};
    std::map<std::string, HostRecord>        mHosts;
};

} // namespace aos::sm::networkmanager

#endif
