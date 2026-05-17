/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_NETWORKMANAGER_DNSNAME_HPP_
#define AOS_SM_NETWORKMANAGER_DNSNAME_HPP_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <Poco/Process.h>

#include <core/common/tools/noncopyable.hpp>
#include <core/sm/networkmanager/itf/dnsname.hpp>

#include <common/process/itf/processspawner.hpp>

#include "dnsserver.hpp"

namespace aos::sm::networkmanager {

/**
 * DNS name factory: owns one dnsmasq process per network.
 */
class DNSName : public DNSNameItf, private NonCopyable {
public:
    /**
     * Initializes the factory.
     *
     * @param dnsStoragePath per-SM DNS root.
     * @param spawner process spawner.
     * @return Error.
     */
    Error Init(const std::string& dnsStoragePath, aos::common::process::ProcessSpawnerItf& spawner);

    /**
     * Reaps DNS servers whose networkID is not in the known set.
     *
     * @param knownNetworkIDs networkIDs still present in storage.
     * @return Error.
     */
    Error RemoveOrphans(const Array<StaticString<cIDLen>>& knownNetworkIDs) override;

    /**
     * Creates or adopts the DNS server for a network.
     *
     * @param networkID network id.
     * @param params per-network DNS configuration.
     * @return RetWithError<DNSServerItf*>.
     */
    RetWithError<DNSServerItf*> CreateServer(const String& networkID, const DNSServerParams& params) override;

    /**
     * Tears down the DNS server for a network.
     *
     * @param networkID network id.
     * @return Error.
     */
    Error RemoveServer(const String& networkID) override;

private:
    static constexpr auto cDnsmasqBinary = "/usr/sbin/dnsmasq";
    static constexpr auto cPidFileName   = "pidfile";
    static constexpr auto cHostsFileName = "addnhosts";

    struct Entry {
        Poco::Process::PID         mPID {};
        std::unique_ptr<DNSServer> mServer;
    };

    std::string                      StorageDirFor(const std::string& networkID) const;
    Error                            EnsureStorageDir(const std::string& dir) const;
    void                             WipeStorageDir(const std::string& dir) const;
    std::vector<std::string>         BuildArgs(const std::string& storageDir, const DNSServerParams& params) const;
    RetWithError<Poco::Process::PID> ReadPidFile(const std::string& storageDir) const;
    bool                             IsDnsmasq(Poco::Process::PID pid, const std::string& storageDir) const;

    std::string                              mDNSStoragePath;
    aos::common::process::ProcessSpawnerItf* mSpawner {};
    std::map<std::string, Entry>             mServers;
};

} // namespace aos::sm::networkmanager

#endif
