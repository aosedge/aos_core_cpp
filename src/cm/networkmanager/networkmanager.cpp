/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <algorithm>
#include <iterator>

#include <core/common/tools/logger.hpp>
#include <core/common/tools/memory.hpp>

#include <common/network/utils.hpp>
#include <common/utils/exception.hpp>
#include <common/utils/parser.hpp>

#include "networkmanager.hpp"

namespace aos::cm::networkmanager {

/***********************************************************************************************************************
 * Constants
 **********************************************************************************************************************/

namespace {

constexpr int cAllowedConnectionsExpectedLen = 3;
constexpr int cExposedPortConfigExpectedLen  = 2;

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

void AppendPort(std::vector<common::utils::PortRange>& ranges, uint16_t port)
{
    if (!ranges.empty() && ranges.back().mLast + 1 == port) {
        ranges.back().mLast = port;

        return;
    }

    ranges.push_back({port, port});
}

std::string PortRangeToString(const common::utils::PortRange& range)
{
    auto result = std::to_string(range.mFirst);

    if (range.mLast != range.mFirst) {
        result += ":" + std::to_string(range.mLast);
    }

    return result;
}

std::string PortRangesToString(const std::vector<common::utils::PortRange>& ranges)
{
    std::string result;

    for (const auto& range : ranges) {
        if (!result.empty()) {
            result += ",";
        }

        result += PortRangeToString(range);
    }

    return result;
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error NetworkManager::Init(StorageItf& storage, crypto::RandomItf& random, DNSServerItf& dnsServer,
    aos::networkmanager::PendingUpdateHandlerItf* pendingUpdateHandler, Duration networkRemoveTimeout)
{
    mStorage              = &storage;
    mRandom               = &random;
    mDNSServer            = &dnsServer;
    mPendingUpdateHandler = pendingUpdateHandler;
    mNetworkRemoveTimeout = std::chrono::nanoseconds(networkRemoveTimeout.Nanoseconds());

    mIpSubnet.Init();

    auto networks = std::make_unique<StaticArray<Network, cMaxNumOwners>>();

    if (auto err = mStorage->GetNetworks(*networks); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    for (const auto& network : *networks) {
        NetworkState networkState;
        networkState.mNetwork = network;

        if (auto err = LoadNetworkHosts(network, networkState); !err.IsNone()) {
            return err;
        }

        if (!HasInstances(networkState)) {
            ScheduleNetworkRemoval(network.mNetworkID.CStr());
        }

        mNetworkStates.try_emplace(network.mNetworkID.CStr(), std::move(networkState));
    }

    RemoveExistedNetworks();

    mUnitNetworks = GetUnitNetworks();

    // Restore pending connections from DB
    auto pendingConnections = std::make_unique<StaticArray<PendingConnection, cMaxNumInstances * cMaxNumConnections>>();

    if (auto err = mStorage->GetAllPendingConnections(*pendingConnections); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    for (const auto& pending : *pendingConnections) {
        mPendingConnections[pending.mRequesterIdent].push_back(pending);
    }

    return ErrorEnum::eNone;
}

Error NetworkManager::Start()
{
    if (mNetworkRemoveTimeout.count() == 0) {
        return ErrorEnum::eNone;
    }

    LOG_DBG() << "Start network manager";

    if (mRemovalThread.joinable()) {
        return AOS_ERROR_WRAP(ErrorEnum::eWrongState);
    }

    try {
        mRemovalStopped = false;
        mRemovalThread  = std::thread(&NetworkManager::RunRemovalThread, this);
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error NetworkManager::Stop()
{
    LOG_DBG() << "Stop network manager";

    {
        std::lock_guard lock {mRemovalMutex};

        mRemovalStopped = true;
    }

    mRemovalCondVar.notify_all();

    if (mRemovalThread.joinable()) {
        mRemovalThread.join();
    }

    return ErrorEnum::eNone;
}

Error NetworkManager::GetNodeNetworkParams(const String& networkID, const String& nodeID, NetworkParams& result)
{
    auto syncUnitNetworks = DeferRelease([this]() { SyncUnitNetworks(); });

    std::lock_guard lock {mMutex};

    LOG_DBG() << "Getting node network params" << Log::Field("networkID", networkID) << Log::Field("nodeID", nodeID);

    try {
        if (auto it = mNetworkStates.find(networkID.CStr()); it != mNetworkStates.end()) {
            if (auto itHost = it->second.mHostInstances.find(nodeID.CStr());
                itHost != it->second.mHostInstances.end()) {
                result.mNetworkID = networkID;
                result.mSubnet    = it->second.mNetwork.mSubnet;
                result.mIP        = itHost->second.mHostInfo.mIP;
                result.mVlanID    = it->second.mNetwork.mVlanID;

                return ErrorEnum::eNone;
            }

            auto IP = mIpSubnet.GetAvailableIP(networkID.CStr());

            result.mNetworkID = networkID;
            result.mSubnet    = it->second.mNetwork.mSubnet;
            result.mIP        = IP.c_str();
            result.mVlanID    = it->second.mNetwork.mVlanID;

            Host host;
            host.mNodeID = nodeID;
            host.mIP     = IP.c_str();

            HostInstances hostInstances;
            hostInstances.mHostInfo = host;

            it->second.mHostInstances.try_emplace(nodeID.CStr(), std::move(hostInstances));

            auto err = mStorage->AddHost(networkID, host);
            AOS_ERROR_CHECK_AND_THROW(err, "error adding host");

            return ErrorEnum::eNone;
        }

        auto vlanID = GenerateVlanID();
        auto subnet = mIpSubnet.GetAvailableSubnet(networkID.CStr());
        auto IP     = mIpSubnet.GetAvailableIP(networkID.CStr());

        result.mNetworkID = networkID;
        result.mSubnet    = subnet.c_str();
        result.mIP        = IP.c_str();
        result.mVlanID    = vlanID;

        Network network;
        network.mNetworkID = networkID;
        network.mSubnet    = subnet.c_str();
        network.mVlanID    = vlanID;

        Host host;
        host.mNodeID = nodeID;
        host.mIP     = IP.c_str();

        HostInstances hostInstances;
        hostInstances.mHostInfo = host;

        NetworkState networkState;
        networkState.mNetwork = network;
        networkState.mHostInstances.try_emplace(nodeID.CStr(), std::move(hostInstances));

        mNetworkStates.try_emplace(networkID.CStr(), std::move(networkState));

        ScheduleNetworkRemoval(networkID.CStr());

        auto err = mStorage->AddNetwork(network);
        AOS_ERROR_CHECK_AND_THROW(err, "error adding network");

        err = mStorage->AddHost(networkID, host);
        AOS_ERROR_CHECK_AND_THROW(err, "error adding host");
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    LOG_DBG() << "Got node network params" << Log::Field("networkID", networkID) << Log::Field("nodeID", nodeID)
              << Log::Field("IP", result.mIP);

    return ErrorEnum::eNone;
}

Error NetworkManager::AllocateInstanceNetwork(const InstanceIdent& instanceIdent, const String& networkID,
    const String& nodeID, const UpdateItemNetworkParams& serviceData, InstanceNetworkAllocation& result)
{
    auto syncUnitNetworks = DeferRelease([this]() { SyncUnitNetworks(); });

    std::unique_lock lock {mMutex};

    LOG_DBG() << "Allocating instance network" << Log::Field("instanceIdent", instanceIdent)
              << Log::Field("networkID", networkID);

    std::vector<std::string> hosts;

    std::transform(serviceData.mHosts.begin(), serviceData.mHosts.end(), std::back_inserter(hosts),
        [](const auto& host) { return host.CStr(); });

    auto targets = GetInstanceTargets(instanceIdent);

    (void)targets.insert(targets.end(), hosts.begin(), hosts.end());

    try {
        std::vector<Connection> connections;
        auto                    it = mNetworkStates.find(networkID.CStr());
        if (it == mNetworkStates.end()) {
            return Error(ErrorEnum::eRuntime, "network not found");
        }

        auto itHost = it->second.mHostInstances.find(nodeID.CStr());
        if (itHost == it->second.mHostInstances.end()) {
            return Error(ErrorEnum::eRuntime, "host not found");
        }

        result.mNetworkID = networkID;
        result.mSubnet    = it->second.mNetwork.mSubnet;

        if (auto itInstance = itHost->second.mInstances.find(instanceIdent);
            itInstance != itHost->second.mInstances.end()) {
            if (auto err = UpdateInstanceNetwork(
                    instanceIdent, networkID, nodeID, serviceData, hosts, it->second, itInstance->second, result);
                !err.IsNone()) {
                return err;
            }

            lock.unlock();
            UpdateRequesters(targets, instanceIdent);

            return ErrorEnum::eNone;
        }

        std::string IP;

        Error err;

        StaticString<cIPLen>                                 migratedIP;
        StaticArray<StaticString<cIPLen>, cMaxNumDNSServers> migratedDNS;

        auto instance = std::make_unique<Instance>();

        instance->mNetworkID     = networkID;
        instance->mNodeID        = nodeID;
        instance->mInstanceIdent = instanceIdent;
        instance->mHosts         = serviceData.mHosts;

        if (MigrateInstanceFromOtherNode(instanceIdent, it->second, nodeID.CStr(), migratedIP, migratedDNS)) {
            IP                    = migratedIP.CStr();
            result.mIP            = migratedIP;
            result.mDNSServers    = migratedDNS;
            instance->mDNSServers = migratedDNS;
        } else {
            auto dnsIP = mDNSServer->GetIP();

            IP         = mIpSubnet.GetAvailableIP(networkID.CStr());
            result.mIP = IP.c_str();
            result.mDNSServers.PushBack(dnsIP.c_str());
            instance->mDNSServers.PushBack(dnsIP.c_str());
        }

        auto rollbackIP = DeferRelease(&IP, [this, &networkID, &err](const std::string* ip) {
            if (!err.IsNone()) {
                mIpSubnet.ReleaseIPToSubnet(networkID.CStr(), *ip);
            }
        });

        instance->mIP = IP.c_str();

        if (err = ParseExposedPorts(serviceData.mExposedPorts, *instance); !err.IsNone()) {
            return err;
        }

        itHost->second.mInstances.try_emplace(instanceIdent, *instance);

        auto rollbackInstance = DeferRelease(&instanceIdent, [this, &itHost, &IP, &err](const InstanceIdent* ident) {
            if (!err.IsNone()) {
                itHost->second.mInstances.erase(*ident);
                mHosts.erase(IP);
            }
        });

        err = PrepareFirewallRules(instanceIdent, it->second.mNetwork.mSubnet.CStr(), IP.c_str(),
            serviceData.mAllowedConnections, result, connections);
        if (!err.IsNone()) {
            return err;
        }

        if (err = AddInstanceHosts(IP, hosts); !err.IsNone()) {
            return err;
        }

        if (err = RestartDNS(); !err.IsNone()) {
            return err;
        }

        if (err = mStorage->AddInstance(*instance); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        (void)mRemovalDeadlines.erase(networkID.CStr());

        StorePendingConnections(
            instanceIdent, nodeID, networkID, result.mIP, it->second.mNetwork.mSubnet.CStr(), connections);

        LOG_DBG() << "Allocated instance network" << Log::Field("networkID", networkID) << Log::Field("nodeID", nodeID)
                  << Log::Field("instanceIdent", instanceIdent) << Log::Field("IP", result.mIP);

    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    lock.unlock();

    UpdateRequesters(targets, instanceIdent);

    return ErrorEnum::eNone;
}

Error NetworkManager::ReleaseInstanceNetwork(const InstanceIdent& instanceIdent, const String& nodeID)
{
    LOG_DBG() << "Releasing instance network" << Log::Field("instanceIdent", instanceIdent);

    std::vector<std::string> targets;

    auto err = RemoveInstance(instanceIdent, nodeID, targets);

    SyncUnitNetworks();

    if (err.IsNone() && targets.empty()) {
        LOG_WRN() << "Instance network parameters not found" << Log::Field("instanceIdent", instanceIdent);

        return ErrorEnum::eNone;
    }

    if (!targets.empty()) {
        UpdateRequesters(targets, instanceIdent);
    }

    return err;
}

Error NetworkManager::ReleaseNodeNetwork(const String& networkID, const String& nodeID)
{
    LOG_DBG() << "Releasing node network" << Log::Field("networkID", networkID) << Log::Field("nodeID", nodeID);

    std::vector<std::string> targets;

    auto err = RemoveNode(networkID, nodeID, targets);

    SyncUnitNetworks();

    if (!targets.empty()) {
        UpdateRequesters(targets, {});
    }

    if (!err.IsNone()) {
        return err;
    }

    LOG_DBG() << "Released node network" << Log::Field("networkID", networkID) << Log::Field("nodeID", nodeID);

    return ErrorEnum::eNone;
}

Error NetworkManager::SyncNetworkState(const String& nodeID, const Array<InstanceNetworkStateInfo>& instances)
{
    LOG_DBG() << "Syncing network state" << Log::Field("nodeID", nodeID)
              << Log::Field("instancesCount", instances.Size());

    std::vector<InstanceIdent> instancesToRelease;

    {
        std::lock_guard lock {mMutex};

        for (auto& [networkID, networkState] : mNetworkStates) {
            auto itHost = networkState.mHostInstances.find(nodeID.CStr());
            if (itHost == networkState.mHostInstances.end()) {
                continue;
            }

            for (const auto& [cmInstanceIdent, cmInstance] : itHost->second.mInstances) {
                auto foundInSM = std::any_of(instances.begin(), instances.end(),
                    [&](const auto& smInstance) { return smInstance.mInstanceIdent == cmInstanceIdent; });

                if (!foundInSM) {
                    LOG_DBG() << "Instance in CM but not in SM, releasing"
                              << Log::Field("instanceIdent", cmInstanceIdent);

                    instancesToRelease.push_back(cmInstanceIdent);
                }
            }
        }
    }

    for (const auto& instanceIdent : instancesToRelease) {
        if (auto err = ReleaseInstanceNetwork(instanceIdent, nodeID); !err.IsNone()) {
            LOG_ERR() << "Failed to release stale instance" << Log::Field("instanceIdent", instanceIdent)
                      << Log::Field(err);
        }
    }

    std::lock_guard updateLock {mUpdateMutex};

    FirewallUpdates updates;

    {
        std::lock_guard lock {mMutex};

        for (const auto& instance : instances) {
            if (mPendingConnections.find(instance.mInstanceIdent) == mPendingConnections.end()) {
                continue;
            }

            auto update = std::make_unique<aos::networkmanager::PendingFirewallUpdate>();

            update->mInstanceIdent = instance.mInstanceIdent;

            GetRequesterRules(instance.mInstanceIdent, update->mFirewallRules);

            const auto missing = std::any_of(update->mFirewallRules.begin(), update->mFirewallRules.end(),
                [&instance](const auto& rule) { return !instance.mFirewallRules.Contains(rule); });
            const auto stale   = std::any_of(instance.mFirewallRules.begin(), instance.mFirewallRules.end(),
                  [this](const auto& rule) { return !IsInstanceIP(rule.mDstIP); });

            if (missing || stale) {
                updates[instance.mInstanceIdent] = std::make_pair(std::string(nodeID.CStr()), *update);
            }
        }
    }

    PushFirewallUpdates(updates);

    return ErrorEnum::eNone;
}

void NetworkManager::OnNodeSubscribed(const String& nodeID)
{
    LOG_DBG() << "Node subscribed to network updates" << Log::Field("nodeID", nodeID);

    std::lock_guard updateLock {mUpdateMutex};

    (void)mSubscribedNodes.insert(nodeID.CStr());

    if (!PushUnitNetworksIfChanged()) {
        PushUnitNetworks({nodeID.CStr()});
    }
}

void NetworkManager::OnNodeUnsubscribed(const String& nodeID)
{
    LOG_DBG() << "Node unsubscribed from network updates" << Log::Field("nodeID", nodeID);

    std::lock_guard updateLock {mUpdateMutex};

    (void)mSubscribedNodes.erase(nodeID.CStr());
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

Error NetworkManager::LoadNetworkHosts(const Network& network, NetworkState& networkState)
{
    auto hosts = std::make_unique<StaticArray<Host, cMaxNumNodes * cMaxNumOwners>>();

    if (auto err = mStorage->GetHosts(network.mNetworkID, *hosts); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    for (const auto& host : *hosts) {
        HostInstances hostInstances;
        hostInstances.mHostInfo = host;

        auto instances = std::make_unique<StaticArray<Instance, cMaxNumInstances>>();

        if (auto err = mStorage->GetInstances(network.mNetworkID, host.mNodeID, *instances); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        for (const auto& instance : *instances) {
            hostInstances.mInstances.try_emplace(instance.mInstanceIdent, instance);

            for (const auto& hostname : instance.mHosts) {
                mHosts[instance.mIP.CStr()].emplace_back(hostname.CStr());
            }
        }

        networkState.mHostInstances.try_emplace(host.mNodeID.CStr(), std::move(hostInstances));
    }

    return ErrorEnum::eNone;
}

Error NetworkManager::UpdateInstanceNetwork(const InstanceIdent& instanceIdent, const String& networkID,
    const String& nodeID, const UpdateItemNetworkParams& serviceData, const std::vector<std::string>& hosts,
    const NetworkState& networkState, Instance& instance, InstanceNetworkAllocation& result)
{
    std::vector<Connection> connections;

    result.mIP         = instance.mIP;
    result.mDNSServers = instance.mDNSServers;

    if (auto err = PrepareFirewallRules(instanceIdent, networkState.mNetwork.mSubnet.CStr(), instance.mIP,
            serviceData.mAllowedConnections, result, connections);
        !err.IsNone()) {
        return err;
    }

    (void)mPendingConnections.erase(instanceIdent);

    mStorage->RemovePendingConnections(instanceIdent);

    StorePendingConnections(
        instanceIdent, nodeID, networkID, instance.mIP, networkState.mNetwork.mSubnet.CStr(), connections);

    Error err;

    auto savedHosts = mHosts[instance.mIP.CStr()];

    auto rollbackHosts = DeferRelease(&err, [this, &instance, &savedHosts](const Error* resultErr) {
        if (!resultErr->IsNone()) {
            mHosts[instance.mIP.CStr()] = savedHosts;
        }
    });

    mHosts.erase(instance.mIP.CStr());

    if (err = AddInstanceHosts(instance.mIP.CStr(), hosts); !err.IsNone()) {
        return err;
    }

    if (err = RestartDNS(); !err.IsNone()) {
        return err;
    }

    if (instance.mHosts != serviceData.mHosts) {
        if (err = mStorage->UpdateInstanceHosts(instanceIdent, serviceData.mHosts); !err.IsNone()) {
            mHosts[instance.mIP.CStr()] = savedHosts;

            if (auto dnsErr = RestartDNS(); !dnsErr.IsNone()) {
                LOG_ERR() << "Failed to restore DNS after hostname update" << Log::Field(dnsErr);
            }

            return AOS_ERROR_WRAP(err);
        }

        instance.mHosts = serviceData.mHosts;
    }

    // Finish the rollback guard before releasing the mutex.
    rollbackHosts.Release();

    return ErrorEnum::eNone;
}

Error NetworkManager::AddInstanceHosts(const std::string& ip, const std::vector<std::string>& hosts)
{
    for (const auto& host : hosts) {
        if (IsHostExist(host)) {
            return Error(ErrorEnum::eAlreadyExist, "host already exists");
        }

        mHosts[ip].push_back(host);
    }

    return ErrorEnum::eNone;
}

Error NetworkManager::ParseExposedPorts(const Array<StaticString<cExposedPortLen>>& exposedPorts, Instance& instance)
{
    for (const auto& exposedPort : exposedPorts) {
        StaticArray<StaticString<cExposedPortLen>, cExposedPortConfigExpectedLen> portConfig;

        if (auto err = exposedPort.Split(portConfig, '/'); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (portConfig.IsEmpty()) {
            return AOS_ERROR_WRAP(Error(ErrorEnum::eRuntime, "unsupported ExposedPorts format"));
        }

        if (const auto exposedPortRange = common::utils::ParsePortRange(portConfig[0].CStr());
            !exposedPortRange.has_value() || exposedPortRange->mFirst != exposedPortRange->mLast) {
            return AOS_ERROR_WRAP(Error(ErrorEnum::eInvalidArgument, "invalid exposed port"));
        }

        ExposedPort exposedPortInfo;
        exposedPortInfo.mPort     = portConfig[0];
        exposedPortInfo.mProtocol = "tcp";

        if (portConfig.Size() == cExposedPortConfigExpectedLen) {
            exposedPortInfo.mProtocol = portConfig[1];
        }

        if (auto err = instance.mExposedPorts.PushBack(exposedPortInfo); !err.IsNone()) {
            return AOS_ERROR_WRAP(Error(err, "too many exposed ports"));
        }
    }

    return ErrorEnum::eNone;
}

void NetworkManager::ParseAllowConnection(
    const String& connection, std::string& target, std::string& port, std::string& protocol)
{
    StaticArray<StaticString<cConnectionNameLen>, cAllowedConnectionsExpectedLen> connConf;

    auto err = connection.Split(connConf, '/');
    AOS_ERROR_CHECK_AND_THROW(err, "error parsing allowed connection");

    if (connConf.Size() < 2) {
        throw std::runtime_error("unsupported allowed connections format");
    }

    target   = connConf[0].CStr();
    port     = connConf[1].CStr();
    protocol = "tcp";

    if (target.empty() || target.size() > cConnectionTargetLen) {
        throw std::runtime_error("invalid allowed connection target");
    }

    if (connConf.Size() == cAllowedConnectionsExpectedLen) {
        protocol = connConf[2].CStr();
    }

    if (!common::utils::ParsePortRange(port).has_value()) {
        throw std::runtime_error("invalid allowed connection port");
    }
}

void NetworkManager::MatchExposedPorts(const Instance& instance, const common::utils::PortRange& requested,
    const std::string& protocol, std::vector<common::utils::PortRange>& exposed,
    std::vector<common::utils::PortRange>& notExposed)
{
    exposed.clear();
    notExposed.clear();

    for (uint32_t checkedPort = requested.mFirst; checkedPort <= requested.mLast; ++checkedPort) {
        const auto exposedFound = std::any_of(instance.mExposedPorts.begin(), instance.mExposedPorts.end(),
            [&protocol, &checkedPort](const auto& exposedPort) {
                if (exposedPort.mProtocol != String(protocol.c_str())) {
                    return false;
                }

                const auto exposedRange = common::utils::ParsePortRange(exposedPort.mPort.CStr());

                return exposedRange.has_value() && exposedRange->mFirst <= checkedPort
                    && checkedPort <= exposedRange->mLast;
            });

        AppendPort(exposedFound ? exposed : notExposed, static_cast<uint16_t>(checkedPort));
    }
}

NetworkManager::ConnectionRules NetworkManager::GetConnectionRules(const std::string& target, const std::string& port,
    const std::string& protocol, const std::string& subnet, const String& ip) const
{
    ConnectionRules result;

    const auto requested = common::utils::ParsePortRange(port);

    if (!requested.has_value()) {
        return result;
    }

    const auto instances = FindTargetInstances(target);

    result.mTargetFound = !instances.empty();

    for (const auto* instance : instances) {
        // instance is in the same subnet could be connected without firewall rules
        if (common::network::NetworkContainsIP(subnet, instance->mIP.CStr())) {
            result.mNotExposedPorts.clear();

            return result;
        }

        std::vector<common::utils::PortRange> exposed;

        MatchExposedPorts(*instance, *requested, protocol, exposed, result.mNotExposedPorts);

        if (exposed.empty()) {
            continue;
        }

        for (const auto& range : exposed) {
            FirewallRule rule;

            rule.mDstIP   = instance->mIP;
            rule.mSrcIP   = ip;
            rule.mProto   = protocol.c_str();
            rule.mDstPort = PortRangeToString(range).c_str();

            result.mRules.push_back(rule);
        }

        return result;
    }

    return result;
}

std::vector<const Instance*> NetworkManager::FindTargetInstances(const std::string& target) const
{
    std::vector<const Instance*> instances;

    for (const auto& [_, networkState] : mNetworkStates) {
        for (const auto& [nodeID, hostInstances] : networkState.mHostInstances) {
            for (const auto& [instanceID, instance] : hostInstances.mInstances) {
                instances.push_back(&instance);
            }
        }
    }

    std::vector<const Instance*> result;

    // A matching item ID takes precedence even when it does not expose the requested port.
    (void)std::copy_if(instances.begin(), instances.end(), std::back_inserter(result),
        [&target](const Instance* instance) { return instance->mInstanceIdent.mItemID == target.c_str(); });

    if (!result.empty()) {
        return result;
    }

    (void)std::copy_if(
        instances.begin(), instances.end(), std::back_inserter(result), [&target](const Instance* instance) {
            return std::any_of(instance->mHosts.begin(), instance->mHosts.end(),
                [&target](const auto& host) { return host == target.c_str(); });
        });

    return result;
}

void NetworkManager::AddConnectionRules(const InstanceIdent& requesterIdent, const std::string& target,
    const std::string& protocol, const ConnectionRules& connectionRules, bool warnNotExposed,
    Array<FirewallRule>& rules) const
{
    for (const auto& rule : connectionRules.mRules) {
        AddFirewallRule(requesterIdent, target, rule, rules);
    }

    if (connectionRules.mNotExposedPorts.empty()) {
        return;
    }

    const char* message = "Allowed connection ports are partially exposed by target, not exposed ports skipped";

    if (connectionRules.mRules.empty()) {
        message = "Allowed connection ports are not exposed by target, no rule created";
    }

    const auto notExposedPorts = PortRangesToString(connectionRules.mNotExposedPorts);

    if (warnNotExposed) {
        LOG_WRN() << message << Log::Field("instanceIdent", requesterIdent) << Log::Field("target", target.c_str())
                  << Log::Field("ports", notExposedPorts.c_str()) << Log::Field("protocol", protocol.c_str());
    } else {
        LOG_DBG() << message << Log::Field("instanceIdent", requesterIdent) << Log::Field("target", target.c_str())
                  << Log::Field("ports", notExposedPorts.c_str()) << Log::Field("protocol", protocol.c_str());
    }
}

Error NetworkManager::PrepareFirewallRules(const InstanceIdent& instanceIdent, const std::string& subnet,
    const String& ip, const Array<StaticString<cConnectionNameLen>>& allowedConnections,
    InstanceNetworkAllocation& result, std::vector<Connection>& connections)
{
    if (allowedConnections.IsEmpty()) {
        return ErrorEnum::eNone;
    }

    try {
        for (const auto& connection : allowedConnections) {
            std::string target;
            std::string port;
            std::string protocol;

            ParseAllowConnection(connection, target, port, protocol);

            AddConnectionRules(instanceIdent, target, protocol, GetConnectionRules(target, port, protocol, subnet, ip),
                true, result.mFirewallRules);

            (void)connections.emplace_back(target, port, protocol);
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

bool NetworkManager::IsHostExist(const std::string& hostName) const
{
    return std::any_of(mHosts.begin(), mHosts.end(), [&hostName](const auto& entry) {
        return std::find(entry.second.begin(), entry.second.end(), hostName) != entry.second.end();
    });
}

uint64_t NetworkManager::GenerateVlanID()
{
    for (int i = 0; i < cVlanGenerateRetries; ++i) {
        auto [vlanID, err] = mRandom->RandInt(cMaxVlanID);
        AOS_ERROR_CHECK_AND_THROW(err, "error generating vlan id");

        if (std::any_of(mNetworkStates.begin(), mNetworkStates.end(),
                [id = vlanID](const auto& item) { return item.second.mNetwork.mVlanID == id; })) {
            continue;
        }

        LOG_DBG() << "Generate vlan ID" << Log::Field("vlanID", vlanID);

        return vlanID;
    }

    throw std::runtime_error("error generating vlan id");
}

bool NetworkManager::MigrateInstanceFromOtherNode(const InstanceIdent& instanceIdent, NetworkState& networkState,
    const std::string& currentNodeID, StaticString<cIPLen>& IP,
    StaticArray<StaticString<cIPLen>, cMaxNumDNSServers>& dnsServers)
{
    for (auto& [otherNodeID, otherHostInstances] : networkState.mHostInstances) {
        if (otherNodeID == currentNodeID) {
            continue;
        }

        auto itInstance = otherHostInstances.mInstances.find(instanceIdent);
        if (itInstance == otherHostInstances.mInstances.end()) {
            continue;
        }

        LOG_DBG() << "Migrating instance" << Log::Field("instanceIdent", instanceIdent)
                  << Log::Field("fromNodeID", otherNodeID.c_str()) << Log::Field("toNodeID", currentNodeID.c_str());

        IP         = itInstance->second.mIP;
        dnsServers = itInstance->second.mDNSServers;

        mIpSubnet.ReleaseIPToSubnet(networkState.mNetwork.mNetworkID.CStr(), IP.CStr());
        mHosts.erase(IP.CStr());

        auto err = mStorage->RemoveNetworkInstance(instanceIdent);
        AOS_ERROR_CHECK_AND_THROW(err, "error removing instance");

        otherHostInstances.mInstances.erase(itInstance);

        (void)mPendingConnections.erase(instanceIdent);

        if (auto pendingErr = mStorage->RemovePendingConnections(instanceIdent); !pendingErr.IsNone()) {
            LOG_ERR() << "Failed to remove pending connections during migration"
                      << Log::Field("instanceIdent", instanceIdent) << Log::Field(pendingErr);
        }

        return true;
    }

    return false;
}

void NetworkManager::RemoveExistedNetworks()
{
    std::vector<std::string> IPs;

    for (const auto& [networkID, networkState] : mNetworkStates) {
        for (const auto& [nodeID, hostInstances] : networkState.mHostInstances) {
            IPs.emplace_back(hostInstances.mHostInfo.mIP.CStr());

            for (const auto& [_, instance] : hostInstances.mInstances) {
                IPs.emplace_back(instance.mIP.CStr());
            }
        }

        mIpSubnet.RemoveAllocatedSubnet(networkID, networkState.mNetwork.mSubnet.CStr(), IPs);
    }
}

Error NetworkManager::RestartDNS()
{
    if (auto err = mDNSServer->UpdateHostsFile(mHosts); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return mDNSServer->Restart();
}

void NetworkManager::StorePendingConnections(const InstanceIdent& requesterIdent, const String& nodeID,
    const String& networkID, const String& ip, const std::string& subnet, const std::vector<Connection>& connections)
{
    if (connections.empty()) {
        return;
    }

    for (const auto& connection : connections) {
        auto pending = std::make_unique<PendingConnection>();

        pending->mRequesterIdent  = requesterIdent;
        pending->mNodeID          = nodeID;
        pending->mNetworkID       = networkID;
        pending->mRequesterIP     = ip;
        pending->mRequesterSubnet = subnet.c_str();
        pending->mTarget          = connection.mTarget.c_str();
        pending->mPort            = connection.mPort.c_str();
        pending->mProtocol        = connection.mProtocol.c_str();

        mPendingConnections[requesterIdent].push_back(*pending);

        if (auto err = mStorage->AddPendingConnection(*pending); !err.IsNone()) {
            LOG_ERR() << "Failed to store pending connection" << Log::Field("instanceIdent", requesterIdent)
                      << Log::Field(err);
        } else {
            LOG_DBG() << "Stored pending connection" << Log::Field("requester", requesterIdent)
                      << Log::Field("target", connection.mTarget.c_str());
        }
    }
}

Error NetworkManager::RemoveInstance(
    const InstanceIdent& instanceIdent, const String& nodeID, std::vector<std::string>& targets)
{
    std::lock_guard lock {mMutex};

    try {
        for (auto& [networkID, networkState] : mNetworkStates) {
            auto itHost = networkState.mHostInstances.find(nodeID.CStr());
            if (itHost == networkState.mHostInstances.end()) {
                continue;
            }

            auto itInstance = itHost->second.mInstances.find(instanceIdent);
            if (itInstance == itHost->second.mInstances.end()) {
                continue;
            }

            auto instanceTargets = GetInstanceTargets(instanceIdent);

            mIpSubnet.ReleaseIPToSubnet(networkID, itInstance->second.mIP.CStr());
            (void)mHosts.erase(itInstance->second.mIP.CStr());

            auto err = mStorage->RemoveNetworkInstance(instanceIdent);
            AOS_ERROR_CHECK_AND_THROW(err, "error removing instance");

            (void)itHost->second.mInstances.erase(itInstance);

            if (!HasInstances(networkState)) {
                ScheduleNetworkRemoval(networkID);
            }

            targets = std::move(instanceTargets);

            (void)mPendingConnections.erase(instanceIdent);

            if (auto pendingErr = mStorage->RemovePendingConnections(instanceIdent); !pendingErr.IsNone()) {
                LOG_ERR() << "Failed to remove pending connections" << Log::Field("instanceIdent", instanceIdent)
                          << Log::Field(pendingErr);
            }

            err = RestartDNS();
            if (err.IsNone()) {
                LOG_DBG() << "Released instance network" << Log::Field("networkID", networkID.c_str())
                          << Log::Field("instanceIdent", instanceIdent);
            }

            return err;
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

Error NetworkManager::RemoveNode(const String& networkID, const String& nodeID, std::vector<std::string>& targets)
{
    std::lock_guard lock {mMutex};

    try {
        auto it = mNetworkStates.find(networkID.CStr());
        if (it == mNetworkStates.end()) {
            return Error(ErrorEnum::eRuntime, "network not found");
        }

        auto itHost = it->second.mHostInstances.find(nodeID.CStr());
        if (itHost == it->second.mHostInstances.end()) {
            return Error(ErrorEnum::eRuntime, "host not found");
        }

        std::vector<std::string> nodeTargets;

        const auto hadInstances = !itHost->second.mInstances.empty();

        for (const auto& [_, instance] : itHost->second.mInstances) {
            const auto instanceTargets = GetInstanceTargets(instance.mInstanceIdent);

            (void)nodeTargets.insert(nodeTargets.end(), instanceTargets.begin(), instanceTargets.end());

            mIpSubnet.ReleaseIPToSubnet(networkID.CStr(), instance.mIP.CStr());
            (void)mHosts.erase(instance.mIP.CStr());

            auto err = mStorage->RemoveNetworkInstance(instance.mInstanceIdent);
            AOS_ERROR_CHECK_AND_THROW(err, "error removing instance");

            (void)mPendingConnections.erase(instance.mInstanceIdent);

            if (auto pendingErr = mStorage->RemovePendingConnections(instance.mInstanceIdent); !pendingErr.IsNone()) {
                LOG_ERR() << "Failed to remove pending connections"
                          << Log::Field("instanceIdent", instance.mInstanceIdent) << Log::Field(pendingErr);
            }
        }

        auto err = mStorage->RemoveHost(networkID, nodeID);
        AOS_ERROR_CHECK_AND_THROW(err, "error removing host");

        (void)it->second.mHostInstances.erase(itHost);

        if (it->second.mHostInstances.empty()) {
            mIpSubnet.ReleaseIPNetPool(networkID.CStr());

            err = mStorage->RemoveNetwork(networkID);
            AOS_ERROR_CHECK_AND_THROW(err, "error removing network");

            (void)mNetworkStates.erase(it);
            (void)mRemovalDeadlines.erase(networkID.CStr());
        } else if (hadInstances && !HasInstances(it->second)) {
            ScheduleNetworkRemoval(networkID.CStr());
        }

        targets = std::move(nodeTargets);

        return RestartDNS();
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }
}

std::vector<std::string> NetworkManager::GetInstanceTargets(const InstanceIdent& instanceIdent) const
{
    std::vector<std::string> targets {instanceIdent.mItemID.CStr()};

    for (const auto& [_, network] : mNetworkStates) {
        for (const auto& [nodeID, host] : network.mHostInstances) {
            if (const auto instance = host.mInstances.find(instanceIdent); instance != host.mInstances.end()) {
                std::transform(instance->second.mHosts.begin(), instance->second.mHosts.end(),
                    std::back_inserter(targets), [](const auto& hostname) { return hostname.CStr(); });
            }
        }
    }

    return targets;
}

bool NetworkManager::IsInstanceIP(const String& ip) const
{
    for (const auto& [_, network] : mNetworkStates) {
        for (const auto& [nodeID, host] : network.mHostInstances) {
            if (std::any_of(host.mInstances.begin(), host.mInstances.end(),
                    [&ip](const auto& instance) { return instance.second.mIP == ip; })) {
                return true;
            }
        }
    }

    return false;
}

void NetworkManager::AddFirewallRule(const InstanceIdent& requesterIdent, const std::string& target,
    const FirewallRule& rule, Array<FirewallRule>& rules) const
{
    if (rules.Contains(rule)) {
        return;
    }

    if (auto err = rules.PushBack(rule); !err.IsNone()) {
        LOG_WRN() << "Too many firewall rules, rule dropped" << Log::Field("instanceIdent", requesterIdent)
                  << Log::Field("target", target.c_str()) << Log::Field("dstIP", rule.mDstIP)
                  << Log::Field("dstPort", rule.mDstPort) << Log::Field(err);
    }
}

void NetworkManager::GetRequesterRules(const InstanceIdent& requesterIdent, Array<FirewallRule>& rules)
{
    auto it = mPendingConnections.find(requesterIdent);
    if (it == mPendingConnections.end()) {
        return;
    }

    for (const auto& connection : it->second) {
        const auto connectionRules = GetConnectionRules(connection.mTarget.CStr(), connection.mPort.CStr(),
            connection.mProtocol.CStr(), connection.mRequesterSubnet.CStr(), connection.mRequesterIP);

        AddConnectionRules(
            requesterIdent, connection.mTarget.CStr(), connection.mProtocol.CStr(), connectionRules, false, rules);
    }
}

void NetworkManager::UpdateRequesters(const std::vector<std::string>& targets, const InstanceIdent& allocatedIdent)
{
    std::lock_guard updateLock {mUpdateMutex};

    FirewallUpdates updates;

    {
        std::lock_guard lock {mMutex};

        std::unordered_map<InstanceIdent, std::string> requesters;

        for (const auto& [requesterIdent, connections] : mPendingConnections) {
            const auto affected = requesterIdent == allocatedIdent
                ? mPushedRequesters.count(requesterIdent) != 0
                : std::any_of(connections.begin(), connections.end(), [&targets](const auto& connection) {
                      return std::find(targets.begin(), targets.end(), connection.mTarget.CStr()) != targets.end();
                  });

            if (affected && !connections.empty()) {
                requesters[requesterIdent] = connections.front().mNodeID.CStr();
            }
        }

        for (const auto& [requesterIdent, nodeID] : requesters) {
            auto& [updateNodeID, update] = updates[requesterIdent];

            updateNodeID          = nodeID;
            update.mInstanceIdent = requesterIdent;

            GetRequesterRules(requesterIdent, update.mFirewallRules);
        }
    }

    PushFirewallUpdates(updates);
}

void NetworkManager::PushFirewallUpdates(const FirewallUpdates& updates)
{
    if (!mPendingUpdateHandler) {
        return;
    }

    for (const auto& [_, updatePair] : updates) {
        const auto& [nodeID, update] = updatePair;

        LOG_DBG() << "Pushing firewall update" << Log::Field("instanceIdent", update.mInstanceIdent)
                  << Log::Field("nodeID", nodeID.c_str()) << Log::Field("rulesCount", update.mFirewallRules.Size());

        mPendingUpdateHandler->OnPendingFirewallUpdate(nodeID.c_str(), update);
        (void)mPushedRequesters.insert(update.mInstanceIdent);
    }
}

bool NetworkManager::HasInstances(const NetworkState& networkState)
{
    return std::any_of(networkState.mHostInstances.begin(), networkState.mHostInstances.end(),
        [](const auto& hostInstances) { return !hostInstances.second.mInstances.empty(); });
}

bool NetworkManager::IsUnitNetwork(const std::string& networkID, const NetworkState& networkState) const
{
    return HasInstances(networkState) || mRemovalDeadlines.count(networkID) != 0;
}

std::vector<std::string> NetworkManager::GetUnitNetworks() const
{
    std::vector<std::string> unitNetworks;

    for (const auto& [networkID, networkState] : mNetworkStates) {
        if (IsUnitNetwork(networkID, networkState)) {
            unitNetworks.push_back(networkID);
        }
    }

    std::sort(unitNetworks.begin(), unitNetworks.end());

    return unitNetworks;
}

void NetworkManager::SyncUnitNetworks()
{
    {
        std::lock_guard updateLock {mUpdateMutex};

        (void)PushUnitNetworksIfChanged();
    }

    NotifyRemovalThread();
}

bool NetworkManager::PushUnitNetworksIfChanged()
{
    std::vector<std::string> unitNetworks;

    {
        std::lock_guard lock {mMutex};

        unitNetworks = GetUnitNetworks();
    }

    if (unitNetworks == mUnitNetworks) {
        return false;
    }

    mUnitNetworks = std::move(unitNetworks);

    PushUnitNetworks(mSubscribedNodes);

    return true;
}

void NetworkManager::PushUnitNetworks(const std::set<std::string>& nodeIDs)
{
    if (!mPendingUpdateHandler) {
        return;
    }

    auto networkIDs = std::make_unique<StaticArray<StaticString<cIDLen>, cMaxNumOwners>>();

    for (const auto& networkID : mUnitNetworks) {
        StaticString<cIDLen> id;

        auto err = id.Assign(networkID.c_str());
        if (err.IsNone()) {
            err = networkIDs->PushBack(id);
        }

        if (!err.IsNone()) {
            LOG_ERR() << "Failed to prepare unit networks update" << Log::Field("networksCount", mUnitNetworks.size())
                      << Log::Field(err);

            return;
        }
    }

    for (const auto& nodeID : nodeIDs) {
        LOG_DBG() << "Pushing unit networks update" << Log::Field("nodeID", nodeID.c_str())
                  << Log::Field("networksCount", networkIDs->Size());

        mPendingUpdateHandler->OnUnitNetworksUpdate(nodeID.c_str(), *networkIDs);
    }
}

void NetworkManager::ScheduleNetworkRemoval(const std::string& networkID)
{
    if (mNetworkRemoveTimeout.count() == 0) {
        return;
    }

    LOG_DBG() << "Schedule network removal" << Log::Field("networkID", networkID.c_str());

    mRemovalDeadlines[networkID] = Clock::now() + mNetworkRemoveTimeout;
}

std::optional<NetworkManager::Clock::time_point> NetworkManager::GetNextRemovalDeadline() const
{
    std::lock_guard lock {mMutex};

    auto it = std::min_element(mRemovalDeadlines.begin(), mRemovalDeadlines.end(),
        [](const auto& left, const auto& right) { return left.second < right.second; });
    if (it == mRemovalDeadlines.end()) {
        return std::nullopt;
    }

    return it->second;
}

void NetworkManager::EraseExpiredDeadlines()
{
    std::lock_guard lock {mMutex};

    const auto now = Clock::now();

    for (auto it = mRemovalDeadlines.begin(); it != mRemovalDeadlines.end();) {
        if (it->second > now) {
            ++it;
            continue;
        }

        LOG_DBG() << "Network removal timeout expired" << Log::Field("networkID", it->first.c_str());

        it = mRemovalDeadlines.erase(it);
    }
}

void NetworkManager::HandleRemovalTimeout()
{
    std::lock_guard updateLock {mUpdateMutex};

    EraseExpiredDeadlines();

    (void)PushUnitNetworksIfChanged();
}

void NetworkManager::NotifyRemovalThread()
{
    {
        std::lock_guard lock {mRemovalMutex};

        mRemovalDeadlinesChanged = true;
    }

    mRemovalCondVar.notify_one();
}

void NetworkManager::RunRemovalThread()
{
    std::unique_lock lock {mRemovalMutex};

    const auto wakeUp = [this]() { return mRemovalStopped || mRemovalDeadlinesChanged; };

    while (!mRemovalStopped) {
        mRemovalDeadlinesChanged = false;

        const auto deadline = GetNextRemovalDeadline();

        if (!deadline) {
            mRemovalCondVar.wait(lock, wakeUp);
            continue;
        }

        if (mRemovalCondVar.wait_until(lock, *deadline, wakeUp)) {
            continue;
        }

        lock.unlock();
        HandleRemovalTimeout();
        lock.lock();
    }
}

} // namespace aos::cm::networkmanager
