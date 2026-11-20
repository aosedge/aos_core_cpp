/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_NETWORKMANAGER_NETWORKMANAGER_HPP_
#define AOS_CM_NETWORKMANAGER_NETWORKMANAGER_HPP_

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <core/common/crypto/itf/crypto.hpp>
#include <core/common/networkmanager/itf/networkprovider.hpp>
#include <core/common/networkmanager/itf/pendingupdatehandler.hpp>
#include <core/common/tools/array.hpp>

#include "ipsubnet.hpp"
#include "itf/dnsserver.hpp"
#include "itf/storage.hpp"
#include "itf/updatesubscriptionlistener.hpp"

namespace aos::cm::networkmanager {

class NetworkManager : public aos::networkmanager::NetworkProviderItf, public UpdateSubscriptionListenerItf {
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
     * @param networkRemoveTimeout Time a network without instances stays in the unit networks (0 - no delay).
     * @return Error.
     */
    Error Init(StorageItf& storage, crypto::RandomItf& random, DNSServerItf& dnsServer,
        aos::networkmanager::PendingUpdateHandlerItf* pendingUpdateHandler = nullptr,
        Duration                                      networkRemoveTimeout = 0);

    /**
     * Starts network manager.
     *
     * @return Error.
     */
    Error Start();

    /**
     * Stops network manager.
     *
     * @return Error.
     */
    Error Stop();

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

    /**
     * Called when node subscribes to network updates.
     *
     * @param nodeID Node ID.
     */
    void OnNodeSubscribed(const String& nodeID) override;

    /**
     * Called when node unsubscribes from network updates.
     *
     * @param nodeID Node ID.
     */
    void OnNodeUnsubscribed(const String& nodeID) override;

private:
    static constexpr uint64_t cMaxVlanID           = 4096;
    static constexpr int      cVlanGenerateRetries = 4;

    void     RemoveExistedNetworks();
    uint64_t GenerateVlanID();
    bool     IsHostExist(const std::string& hostName) const;

    struct Connection {
        std::string mTarget;
        std::string mPort;
        std::string mProtocol;

        Connection(std::string target, std::string port, std::string protocol)
            : mTarget(std::move(target))
            , mPort(std::move(port))
            , mProtocol(std::move(protocol))
        {
        }
    };

    using FirewallUpdates
        = std::unordered_map<InstanceIdent, std::pair<std::string, aos::networkmanager::PendingFirewallUpdate>>;

    std::optional<FirewallRule> GetInstanceRule(const std::string& target, const std::string& port,
        const std::string& protocol, const std::string& subnet, const String& ip, bool& instanceFound);
    bool  RuleExists(const Instance& instance, const std::string& port, const std::string& protocol);
    void  ParseAllowConnection(const String& connection, std::string& target, std::string& port, std::string& protocol);
    Error PrepareFirewallRules(const InstanceIdent& instanceIdent, const std::string& subnet, const String& ip,
        const Array<StaticString<cConnectionNameLen>>& allowedConnections, InstanceNetworkAllocation& result,
        std::vector<Connection>& connections);
    void  StorePendingConnections(const InstanceIdent& requesterIdent, const String& nodeID, const String& networkID,
         const String& ip, const std::string& subnet, const std::vector<Connection>& connections);
    bool  IsInstanceIP(const String& ip) const;
    void  AddFirewallRule(const InstanceIdent& requesterIdent, const std::string& target, const FirewallRule& rule,
         Array<FirewallRule>& rules) const;
    void  GetRequesterRules(const InstanceIdent& requesterIdent, Array<FirewallRule>& rules);
    void  UpdateRequesters(const std::vector<std::string>& targets, const InstanceIdent& allocatedIdent);
    void  PushFirewallUpdates(const FirewallUpdates& updates);

    Error RemoveInstance(const InstanceIdent& instanceIdent, const String& nodeID, std::vector<std::string>& targets);
    Error RemoveNode(const String& networkID, const String& nodeID, std::vector<std::string>& targets);

    std::vector<std::string> GetInstanceTargets(const InstanceIdent& instanceIdent) const;

    bool  MigrateInstanceFromOtherNode(const InstanceIdent& instanceIdent, NetworkState& networkState,
         const std::string& currentNodeID, StaticString<cIPLen>& ip,
         StaticArray<StaticString<cIPLen>, cMaxNumDNSServers>& dnsServers);
    Error ParseExposedPorts(const Array<StaticString<cExposedPortLen>>& exposedPorts, Instance& instance);

    Error RestartDNS();

    using Clock = std::chrono::steady_clock;

    static bool              HasInstances(const NetworkState& networkState);
    bool                     IsUnitNetwork(const std::string& networkID, const NetworkState& networkState) const;
    std::vector<std::string> GetUnitNetworks() const;
    void                     SyncUnitNetworks();
    bool                     PushUnitNetworksIfChanged();
    void                     PushUnitNetworks(const std::set<std::string>& nodeIDs);

    void                             ScheduleNetworkRemoval(const std::string& networkID);
    std::optional<Clock::time_point> GetNextRemovalDeadline() const;
    void                             EraseExpiredDeadlines();
    void                             HandleRemovalTimeout();
    void                             NotifyRemovalThread();
    void                             RunRemovalThread();

    StorageItf*        mStorage {};
    crypto::RandomItf* mRandom {};
    DNSServerItf*      mDNSServer {};
    IpSubnet           mIpSubnet;

    mutable std::mutex                                                mMutex;
    std::mutex                                                        mUpdateMutex;
    std::unordered_map<std::string, NetworkState>                     mNetworkStates;
    std::unordered_map<std::string, std::vector<std::string>>         mHosts;
    std::unordered_map<InstanceIdent, std::vector<PendingConnection>> mPendingConnections;
    std::unordered_set<InstanceIdent>                                 mPushedRequesters;
    aos::networkmanager::PendingUpdateHandlerItf*                     mPendingUpdateHandler {};
    std::vector<std::string>                                          mUnitNetworks;
    std::set<std::string>                                             mSubscribedNodes;
    std::chrono::nanoseconds                                          mNetworkRemoveTimeout {};
    std::unordered_map<std::string, Clock::time_point>                mRemovalDeadlines;
    std::mutex                                                        mRemovalMutex;
    std::condition_variable                                           mRemovalCondVar;
    std::thread                                                       mRemovalThread;
    bool                                                              mRemovalStopped {};
    bool                                                              mRemovalDeadlinesChanged {};
};

} // namespace aos::cm::networkmanager
#endif
