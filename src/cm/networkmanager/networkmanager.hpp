/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_NETWORKMANAGER_NETWORKMANAGER_HPP_
#define AOS_CM_NETWORKMANAGER_NETWORKMANAGER_HPP_

#include <optional>
#include <string_view>
#include <unordered_map>

#include <core/cm/networkmanager/itf/networkmanager.hpp>
#include <core/cm/networkmanager/itf/nodenetwork.hpp>
#include <core/common/crypto/itf/crypto.hpp>
#include <core/common/tools/array.hpp>

#include "ipsubnet.hpp"
#include "itf/dnsserver.hpp"
#include "itf/storage.hpp"

namespace aos::cm::networkmanager {

class NetworkManager : public NetworkManagerItf {
public:
    /**
     * Constructor.
     */
    NetworkManager() = default;

    /**
     * Initializes network manager.
     *
     * @param storage Storage interface.
     * @param random Random interface.
     * @param nodeNetwork Node network interface.
     * @param dnsServer DNS server interface.
     * @return Error.
     */
    Error Init(StorageItf& storage, crypto::RandomItf& random, NodeNetworkItf& nodeNetwork, DNSServerItf& dnsServer);

    /**
     * Gets instances.
     *
     * @param[out] instances Instances.
     * @return Error.
     */
    Error GetInstances(Array<InstanceIdent>& instances) const override;

    /**
     * Removes instance network parameters.
     *
     * @param instanceIdent Instance identifier.
     * @param nodeID Node ID.
     * @return Error.
     */
    Error RemoveInstanceNetworkParameters(const InstanceIdent& instanceIdent, const String& nodeID) override;

    /**
     * Updates provider network.
     *
     * @param providers Providers.
     * @param nodeID Node ID.
     * @return Error.
     */
    Error UpdateProviderNetwork(const Array<StaticString<cIDLen>>& providers, const String& nodeID) override;

    /**
     * Prepares instance network parameters.
     *
     * @param instanceIdent Instance identifier.
     * @param networkID Network ID.
     * @param nodeID Node ID.
     * @param instanceData Instance data.
     * @param[out] result Result.
     * @return Error.
     */
    Error PrepareInstanceNetworkParameters(const InstanceIdent& instanceIdent, const String& networkID,
        const String& nodeID, const NetworkServiceData& networkData, InstanceNetworkParameters& result) override;

    /**
     * Restarts DNS server.
     *
     * @return Error.
     */
    Error RestartDNSServer() override;

private:
    static constexpr uint64_t cMaxVlanID           = 4096;
    static constexpr int      cVlanGenerateRetries = 4;

    void RemoveExistedNetworks();
    void RemoveProviderNetworks(const Array<StaticString<cIDLen>>& providers, const String& nodeID);
    void AddProviderNetwork(const String& networkID, const String& nodeID, UpdateNetworkParameters& networkParameters);
    void RemoveInstanceNetwork(const std::string& networkID, const std::string& IP, const InstanceIdent& instanceIdent);
    void CreateProviderNetwork(
        const String& networkID, const String& nodeID, UpdateNetworkParameters& networkParameters);
    bool     ShouldRemoveNetwork(const NetworkState& networkState, const Array<StaticString<cIDLen>>& providers) const;
    void     CleanupHostFromNetwork(NetworkState& networkState, const String& nodeID);
    bool     IsNetworkEmpty(const NetworkState& networkState) const;
    void     CleanupEmptyNetwork(const NetworkState& networkState);
    uint64_t GenerateVlanID();
    void     PrepareInstanceIdentHosts(
            const InstanceIdent& instanceIdent, const String& networkID, std::vector<std::string>& hosts) const;
    bool  IsHostExist(const std::string& hostName) const;
    Error AddHosts(const std::vector<std::string>& hosts, const std::string& ip);

    std::optional<FirewallRule> GetInstanceRule(const std::string& itemID, const std::string& port,
        const std::string& protocol, const std::string& subnet, const String& ip);
    bool  RuleExists(const Instance& instance, const std::string& port, const std::string& protocol);
    void  ParseAllowConnection(const String& connection, std::string& itemID, std::string& port, std::string& protocol);
    Error PrepareFirewallRules(const std::string& subnet, const String& ip,
        const Array<StaticString<cConnectionNameLen>>& allowedConnections, InstanceNetworkParameters& result);

    bool  MigrateInstanceFromOtherNode(const InstanceIdent& instanceIdent, NetworkState& networkState,
         const std::string& currentNodeID, StaticString<cIPLen>& ip,
         StaticArray<StaticString<cIPLen>, cMaxNumDNSServers>& dnsServers);
    Error ParseExposedPorts(const Array<StaticString<cExposedPortLen>>& exposedPorts, Instance& instance);

    StorageItf*        mStorage {};
    crypto::RandomItf* mRandom {};
    NodeNetworkItf*    mNodeNetwork {};
    DNSServerItf*      mDNSServer {};
    IpSubnet           mIpSubnet;

    std::unordered_map<std::string, NetworkState>             mNetworkStates;
    std::unordered_map<std::string, std::vector<std::string>> mHosts;
};

} // namespace aos::cm::networkmanager
#endif
