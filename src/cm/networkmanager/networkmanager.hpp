/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_NETWORKMANAGER_NETWORKMANAGER_HPP_
#define AOS_CM_NETWORKMANAGER_NETWORKMANAGER_HPP_

#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <core/common/crypto/itf/crypto.hpp>
#include <core/common/networkmanager/itf/networkprovider.hpp>
#include <core/common/networkmanager/itf/pendingupdatehandler.hpp>
#include <core/common/tools/array.hpp>

#include "ipsubnet.hpp"
#include "itf/dnsserver.hpp"
#include "itf/storage.hpp"

namespace aos::cm::networkmanager {

class NetworkManager : public aos::networkmanager::NetworkProviderItf {
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
     * @param dnsServer DNS server interface.
     * @param pendingUpdateHandler Pending firewall update handler (optional).
     * @return Error.
     */
    Error Init(StorageItf& storage, crypto::RandomItf& random, DNSServerItf& dnsServer,
        aos::networkmanager::PendingUpdateHandlerItf* pendingUpdateHandler = nullptr);

    /**
     * Gets node network parameters.
     *
     * @param networkID Network ID.
     * @param nodeID Node ID.
     * @param[out] result Network parameters.
     * @return Error.
     */
    Error GetNodeNetworkParams(const String& networkID, const String& nodeID, NetworkParams& result) override;

    /**
     * Allocates instance network.
     *
     * @param instanceIdent Instance identifier.
     * @param networkID Network ID.
     * @param nodeID Node ID.
     * @param serviceData Network service data.
     * @param[out] result Instance network parameters.
     * @return Error.
     */
    Error AllocateInstanceNetwork(const InstanceIdent& instanceIdent, const String& networkID, const String& nodeID,
        const UpdateItemNetworkParams& serviceData, InstanceNetworkAllocation& result) override;

    /**
     * Releases instance network.
     *
     * @param instanceIdent Instance identifier.
     * @param nodeID Node ID.
     * @return Error.
     */
    Error ReleaseInstanceNetwork(const InstanceIdent& instanceIdent, const String& nodeID) override;

    /**
     * Releases node network.
     *
     * @param networkID Network ID.
     * @param nodeID Node ID.
     * @return Error.
     */
    Error ReleaseNodeNetwork(const String& networkID, const String& nodeID) override;

    /**
     * Reconciles network state with SM on (re)connect.
     *
     * @param nodeID Node ID.
     * @param instances Array of instance network state from SM.
     * @return Error.
     */
    Error SyncNetworkState(const String& nodeID, const Array<InstanceNetworkStateInfo>& instances) override;

private:
    static constexpr uint64_t cMaxVlanID           = 4096;
    static constexpr int      cVlanGenerateRetries = 4;

    void     RemoveExistedNetworks();
    uint64_t GenerateVlanID();
    bool     IsHostExist(const std::string& hostName) const;

    struct UnresolvedConnection {
        std::string mItemID;
        std::string mPort;
        std::string mProtocol;

        UnresolvedConnection(std::string itemID, std::string port, std::string protocol)
            : mItemID(std::move(itemID))
            , mPort(std::move(port))
            , mProtocol(std::move(protocol))
        {
        }
    };

    std::optional<FirewallRule> GetInstanceRule(const std::string& itemID, const std::string& port,
        const std::string& protocol, const std::string& subnet, const String& ip, bool& instanceFound);
    bool  RuleExists(const Instance& instance, const std::string& port, const std::string& protocol);
    void  ParseAllowConnection(const String& connection, std::string& itemID, std::string& port, std::string& protocol);
    Error PrepareFirewallRules(const std::string& subnet, const String& ip,
        const Array<StaticString<cConnectionNameLen>>& allowedConnections, InstanceNetworkAllocation& result,
        std::vector<UnresolvedConnection>& unresolvedConnections);
    void  StorePendingConnections(const InstanceIdent& requesterIdent, const String& nodeID, const String& networkID,
         const String& ip, const std::string& subnet, const std::vector<UnresolvedConnection>& unresolvedConnections);
    void  ResolvePendingConnections(const InstanceIdent& newInstanceIdent);
    void  ReloadPendingConnections(const String& nodeID);
    void  CleanConfirmedPendingConnections(const String& nodeID, const Array<InstanceNetworkStateInfo>& instances);

    bool  MigrateInstanceFromOtherNode(const InstanceIdent& instanceIdent, NetworkState& networkState,
         const std::string& currentNodeID, StaticString<cIPLen>& ip,
         StaticArray<StaticString<cIPLen>, cMaxNumDNSServers>& dnsServers);
    Error ParseExposedPorts(const Array<StaticString<cExposedPortLen>>& exposedPorts, Instance& instance);

    Error RestartDNS();

    StorageItf*        mStorage {};
    crypto::RandomItf* mRandom {};
    DNSServerItf*      mDNSServer {};
    IpSubnet           mIpSubnet;

    mutable std::mutex                                        mMutex;
    std::unordered_map<std::string, NetworkState>             mNetworkStates;
    std::unordered_map<std::string, std::vector<std::string>> mHosts;
    std::unordered_multimap<std::string, PendingConnection>   mPendingConnections;
    aos::networkmanager::PendingUpdateHandlerItf*             mPendingUpdateHandler {};
};

} // namespace aos::cm::networkmanager
#endif
