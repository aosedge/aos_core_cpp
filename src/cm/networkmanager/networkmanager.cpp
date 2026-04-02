/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <algorithm>

#include <core/common/tools/logger.hpp>
#include <core/common/tools/memory.hpp>

#include <common/network/utils.hpp>
#include <common/utils/exception.hpp>

#include "networkmanager.hpp"

namespace aos::cm::networkmanager {

/***********************************************************************************************************************
 * Constants
 **********************************************************************************************************************/

namespace {

constexpr int cAllowedConnectionsExpectedLen = 3;
constexpr int cExposedPortConfigExpectedLen  = 2;

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error NetworkManager::Init(StorageItf& storage, crypto::RandomItf& random, DNSServerItf& dnsServer,
    aos::networkmanager::PendingUpdateHandlerItf* pendingUpdateHandler)
{
    mStorage              = &storage;
    mRandom               = &random;
    mDNSServer            = &dnsServer;
    mPendingUpdateHandler = pendingUpdateHandler;

    mIpSubnet.Init();

    auto networks = std::make_unique<StaticArray<Network, cMaxNumOwners>>();

    if (auto err = mStorage->GetNetworks(*networks); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    for (const auto& network : *networks) {
        NetworkState networkState;
        networkState.mNetwork = network;

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
                hostInstances.mInstances.emplace(instance.mInstanceIdent, instance);
            }

            networkState.mHostInstances.emplace(host.mNodeID.CStr(), std::move(hostInstances));
        }

        mNetworkStates.emplace(network.mNetworkID.CStr(), std::move(networkState));
    }

    RemoveExistedNetworks();

    // Restore pending connections from DB
    auto pendingConnections = std::make_unique<StaticArray<PendingConnection, cMaxNumInstances * cMaxNumConnections>>();

    if (auto err = mStorage->GetAllPendingConnections(*pendingConnections); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    for (const auto& pending : *pendingConnections) {
        mPendingConnections.emplace(pending.mTargetItemID.CStr(), pending);
    }

    return ErrorEnum::eNone;
}

Error NetworkManager::GetNodeNetworkParams(const String& networkID, const String& nodeID, NetworkParams& result)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Getting node network params" << Log::Field("networkID", networkID) << Log::Field("nodeID", nodeID);

    try {
        auto it = mNetworkStates.find(networkID.CStr());
        if (it != mNetworkStates.end()) {
            auto itHost = it->second.mHostInstances.find(nodeID.CStr());
            if (itHost != it->second.mHostInstances.end()) {
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

            it->second.mHostInstances.emplace(nodeID.CStr(), std::move(hostInstances));

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
        networkState.mHostInstances.emplace(nodeID.CStr(), std::move(hostInstances));

        mNetworkStates.emplace(networkID.CStr(), std::move(networkState));

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
    std::unique_lock lock {mMutex};

    LOG_DBG() << "Allocating instance network" << Log::Field("instanceIdent", instanceIdent)
              << Log::Field("networkID", networkID);

    std::vector<std::string> hosts;

    std::transform(serviceData.mHosts.begin(), serviceData.mHosts.end(), std::back_inserter(hosts),
        [](const auto& host) { return host.CStr(); });

    try {
        std::vector<UnresolvedConnection> unresolvedConnections;
        auto                              it = mNetworkStates.find(networkID.CStr());
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
            result.mIP         = itInstance->second.mIP;
            result.mDNSServers = itInstance->second.mDNSServers;

            if (auto err = PrepareFirewallRules(it->second.mNetwork.mSubnet.CStr(), itInstance->second.mIP,
                    serviceData.mAllowedConnections, result, unresolvedConnections);
                !err.IsNone()) {
                return err;
            }

            for (auto pendIt = mPendingConnections.begin(); pendIt != mPendingConnections.end();) {
                if (pendIt->second.mRequesterIdent == instanceIdent) {
                    pendIt = mPendingConnections.erase(pendIt);
                } else {
                    ++pendIt;
                }
            }

            mStorage->RemovePendingConnections(instanceIdent);

            StorePendingConnections(instanceIdent, nodeID, networkID, itInstance->second.mIP,
                it->second.mNetwork.mSubnet.CStr(), unresolvedConnections);

            Error err;

            auto savedHosts = mHosts[itInstance->second.mIP.CStr()];

            auto rollbackHosts = DeferRelease(&err, [this, &itInstance, &savedHosts](const Error* err) {
                if (!err->IsNone()) {
                    mHosts[itInstance->second.mIP.CStr()] = savedHosts;
                }
            });

            mHosts.erase(itInstance->second.mIP.CStr());

            for (const auto& host : hosts) {
                if (IsHostExist(host)) {
                    err = Error(ErrorEnum::eAlreadyExist, "host already exists");

                    return err;
                }

                mHosts[itInstance->second.mIP.CStr()].push_back(host);
            }

            err = RestartDNS();

            return err;
        }

        std::string IP;

        Error err;

        StaticString<cIPLen>                                 migratedIP;
        StaticArray<StaticString<cIPLen>, cMaxNumDNSServers> migratedDNS;
        Instance                                             instance;

        instance.mNetworkID     = networkID;
        instance.mNodeID        = nodeID;
        instance.mInstanceIdent = instanceIdent;

        if (MigrateInstanceFromOtherNode(instanceIdent, it->second, nodeID.CStr(), migratedIP, migratedDNS)) {
            IP                   = migratedIP.CStr();
            result.mIP           = migratedIP;
            result.mDNSServers   = migratedDNS;
            instance.mDNSServers = migratedDNS;
        } else {
            auto dnsIP = mDNSServer->GetIP();

            IP         = mIpSubnet.GetAvailableIP(networkID.CStr());
            result.mIP = IP.c_str();
            result.mDNSServers.PushBack(dnsIP.c_str());
            instance.mDNSServers.PushBack(dnsIP.c_str());
        }

        auto rollbackIP = DeferRelease(&IP, [this, &networkID, &err](const std::string* ip) {
            if (!err.IsNone()) {
                mIpSubnet.ReleaseIPToSubnet(networkID.CStr(), *ip);
            }
        });

        instance.mIP = IP.c_str();

        if (err = ParseExposedPorts(serviceData.mExposedPorts, instance); !err.IsNone()) {
            return err;
        }

        itHost->second.mInstances.emplace(instanceIdent, instance);

        auto rollbackInstance = DeferRelease(&instanceIdent, [this, &itHost, &IP, &err](const InstanceIdent* ident) {
            if (!err.IsNone()) {
                itHost->second.mInstances.erase(*ident);
                mHosts.erase(IP);
            }
        });

        if (err = PrepareFirewallRules(it->second.mNetwork.mSubnet.CStr(), IP.c_str(), serviceData.mAllowedConnections,
                result, unresolvedConnections);
            !err.IsNone()) {
            return err;
        }

        for (const auto& host : hosts) {
            if (IsHostExist(host)) {
                err = Error(ErrorEnum::eAlreadyExist, "host already exists");
                return err;
            }

            mHosts[IP].push_back(host);
        }

        if (err = RestartDNS(); !err.IsNone()) {
            return err;
        }

        if (err = mStorage->AddInstance(instance); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        StorePendingConnections(
            instanceIdent, nodeID, networkID, result.mIP, it->second.mNetwork.mSubnet.CStr(), unresolvedConnections);

        LOG_DBG() << "Allocated instance network" << Log::Field("networkID", networkID) << Log::Field("nodeID", nodeID)
                  << Log::Field("instanceIdent", instanceIdent) << Log::Field("IP", result.mIP);

    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    lock.unlock();

    ResolvePendingConnections(instanceIdent);

    return ErrorEnum::eNone;
}

Error NetworkManager::ReleaseInstanceNetwork(const InstanceIdent& instanceIdent, const String& nodeID)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Releasing instance network" << Log::Field("instanceIdent", instanceIdent);

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

            mIpSubnet.ReleaseIPToSubnet(networkID, itInstance->second.mIP.CStr());
            mHosts.erase(itInstance->second.mIP.CStr());

            auto err = mStorage->RemoveNetworkInstance(instanceIdent);
            AOS_ERROR_CHECK_AND_THROW(err, "error removing instance");

            itHost->second.mInstances.erase(itInstance);

            // Remove pending connections where this instance is the requester
            for (auto it = mPendingConnections.begin(); it != mPendingConnections.end();) {
                if (it->second.mRequesterIdent == instanceIdent) {
                    it = mPendingConnections.erase(it);
                } else {
                    ++it;
                }
            }

            if (auto pendingErr = mStorage->RemovePendingConnections(instanceIdent); !pendingErr.IsNone()) {
                LOG_ERR() << "Failed to remove pending connections" << Log::Field("instanceIdent", instanceIdent)
                          << Log::Field(pendingErr);
            }

            if (auto dnsErr = RestartDNS(); !dnsErr.IsNone()) {
                return dnsErr;
            }

            LOG_DBG() << "Released instance network" << Log::Field("networkID", networkID.c_str())
                      << Log::Field("instanceIdent", instanceIdent);

            return ErrorEnum::eNone;
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    LOG_WRN() << "Instance network parameters not found" << Log::Field("instanceIdent", instanceIdent);

    return ErrorEnum::eNone;
}

Error NetworkManager::ReleaseNodeNetwork(const String& networkID, const String& nodeID)
{
    std::lock_guard lock {mMutex};

    LOG_DBG() << "Releasing node network" << Log::Field("networkID", networkID) << Log::Field("nodeID", nodeID);

    try {
        auto it = mNetworkStates.find(networkID.CStr());
        if (it == mNetworkStates.end()) {
            return Error(ErrorEnum::eRuntime, "network not found");
        }

        auto itHost = it->second.mHostInstances.find(nodeID.CStr());
        if (itHost == it->second.mHostInstances.end()) {
            return Error(ErrorEnum::eRuntime, "host not found");
        }

        for (auto& [_, instance] : itHost->second.mInstances) {
            mIpSubnet.ReleaseIPToSubnet(networkID.CStr(), instance.mIP.CStr());
            mHosts.erase(instance.mIP.CStr());

            auto err = mStorage->RemoveNetworkInstance(instance.mInstanceIdent);
            AOS_ERROR_CHECK_AND_THROW(err, "error removing instance");

            for (auto pendIt = mPendingConnections.begin(); pendIt != mPendingConnections.end();) {
                if (pendIt->second.mRequesterIdent == instance.mInstanceIdent) {
                    pendIt = mPendingConnections.erase(pendIt);
                } else {
                    ++pendIt;
                }
            }

            if (auto pendingErr = mStorage->RemovePendingConnections(instance.mInstanceIdent); !pendingErr.IsNone()) {
                LOG_ERR() << "Failed to remove pending connections"
                          << Log::Field("instanceIdent", instance.mInstanceIdent) << Log::Field(pendingErr);
            }
        }

        auto err = mStorage->RemoveHost(networkID, nodeID);
        AOS_ERROR_CHECK_AND_THROW(err, "error removing host");

        it->second.mHostInstances.erase(itHost);

        if (it->second.mHostInstances.empty()) {
            mIpSubnet.ReleaseIPNetPool(networkID.CStr());

            err = mStorage->RemoveNetwork(networkID);
            AOS_ERROR_CHECK_AND_THROW(err, "error removing network");

            mNetworkStates.erase(it);
        }

        if (auto dnsErr = RestartDNS(); !dnsErr.IsNone()) {
            return dnsErr;
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
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

    std::vector<InstanceIdent> allInstances;

    {
        std::lock_guard lock {mMutex};

        CleanConfirmedPendingConnections(nodeID, instances);

        ReloadPendingConnections(nodeID);

        for (const auto& [networkID, networkState] : mNetworkStates) {
            for (const auto& [hostNodeID, hostInstances] : networkState.mHostInstances) {
                for (const auto& [instanceIdent, instance] : hostInstances.mInstances) {
                    allInstances.push_back(instanceIdent);
                }
            }
        }
    }

    for (const auto& instanceIdent : allInstances) {
        ResolvePendingConnections(instanceIdent);
    }

    return ErrorEnum::eNone;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

Error NetworkManager::ParseExposedPorts(const Array<StaticString<cExposedPortLen>>& exposedPorts, Instance& instance)
{
    for (const auto& exposedPort : exposedPorts) {
        StaticArray<StaticString<cExposedPortLen>, cExposedPortConfigExpectedLen> portConfig;

        if (auto err = exposedPort.Split(portConfig, '/'); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (portConfig.Size() == 0) {
            return AOS_ERROR_WRAP(Error(ErrorEnum::eRuntime, "unsupported ExposedPorts format"));
        }

        ExposedPort exposedPortInfo;
        exposedPortInfo.mPort     = portConfig[0];
        exposedPortInfo.mProtocol = "tcp";

        if (portConfig.Size() == cExposedPortConfigExpectedLen) {
            exposedPortInfo.mProtocol = portConfig[1];
        }

        instance.mExposedPorts.PushBack(exposedPortInfo);
    }

    return ErrorEnum::eNone;
}

void NetworkManager::ParseAllowConnection(
    const String& connection, std::string& itemID, std::string& port, std::string& protocol)
{
    StaticArray<StaticString<cConnectionNameLen>, cAllowedConnectionsExpectedLen> connConf;

    auto err = connection.Split(connConf, '/');
    AOS_ERROR_CHECK_AND_THROW(err, "error parsing allowed connection");

    if (connConf.Size() < 2) {
        throw std::runtime_error("unsupported allowed connections format");
    }

    itemID   = connConf[0].CStr();
    port     = connConf[1].CStr();
    protocol = "tcp";

    if (connConf.Size() == cAllowedConnectionsExpectedLen) {
        protocol = connConf[2].CStr();
    }
}

bool NetworkManager::RuleExists(const Instance& instance, const std::string& port, const std::string& protocol)
{
    return std::any_of(instance.mExposedPorts.begin(), instance.mExposedPorts.end(), [&](const auto& exposedPort) {
        return exposedPort.mPort == String(port.c_str()) && exposedPort.mProtocol == String(protocol.c_str());
    });
}

std::optional<FirewallRule> NetworkManager::GetInstanceRule(const std::string& itemID, const std::string& port,
    const std::string& protocol, const std::string& subnet, const String& ip, bool& instanceFound)
{
    instanceFound = false;

    for (auto& [_, networkState] : mNetworkStates) {
        for (auto& [nodeID, hostInstances] : networkState.mHostInstances) {
            for (auto& [instanceID, instance] : hostInstances.mInstances) {
                if (instance.mInstanceIdent.mItemID != itemID.c_str()) {
                    continue;
                }

                instanceFound = true;

                // instance is in the same subnet could be connected without firewall rules
                if (common::network::NetworkContainsIP(subnet, instance.mIP.CStr())) {
                    return std::nullopt;
                }

                if (RuleExists(instance, port, protocol)) {
                    FirewallRule rule;
                    rule.mDstIP   = instance.mIP;
                    rule.mSrcIP   = ip;
                    rule.mProto   = protocol.c_str();
                    rule.mDstPort = port.c_str();

                    return rule;
                }
            }
        }
    }

    return std::nullopt;
}

Error NetworkManager::PrepareFirewallRules(const std::string& subnet, const String& ip,
    const Array<StaticString<cConnectionNameLen>>& allowedConnections, InstanceNetworkAllocation& result,
    std::vector<UnresolvedConnection>& unresolvedConnections)
{
    if (allowedConnections.IsEmpty()) {
        return ErrorEnum::eNone;
    }

    try {
        for (const auto& connection : allowedConnections) {
            std::string itemID, port, protocol;
            ParseAllowConnection(connection, itemID, port, protocol);

            bool instanceFound = false;
            auto rule          = GetInstanceRule(itemID, port, protocol, subnet, ip, instanceFound);

            if (rule) {
                result.mFirewallRules.PushBack(*rule);
            } else if (!instanceFound) {
                unresolvedConnections.emplace_back(itemID, port, protocol);
            }
        }
    } catch (const std::exception& e) {
        return AOS_ERROR_WRAP(common::utils::ToAosError(e));
    }

    return ErrorEnum::eNone;
}

bool NetworkManager::IsHostExist(const std::string& hostName) const
{
    for (const auto& [_, hosts] : mHosts) {
        if (std::find(hosts.begin(), hosts.end(), hostName) != hosts.end()) {
            return true;
        }
    }

    return false;
}

uint64_t NetworkManager::GenerateVlanID()
{
    for (int i = 0; i < cVlanGenerateRetries; ++i) {
        auto [vlanID, err] = mRandom->RandInt(cMaxVlanID);
        AOS_ERROR_CHECK_AND_THROW(err, "error generating vlan id");

        for (auto& [_, networkState] : mNetworkStates) {
            if (networkState.mNetwork.mVlanID == vlanID) {
                continue;
            }
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

        for (auto pendIt = mPendingConnections.begin(); pendIt != mPendingConnections.end();) {
            if (pendIt->second.mRequesterIdent == instanceIdent) {
                pendIt = mPendingConnections.erase(pendIt);
            } else {
                ++pendIt;
            }
        }

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

    for (auto& [networkID, networkState] : mNetworkStates) {
        for (auto& [nodeID, hostInstances] : networkState.mHostInstances) {
            IPs.push_back(hostInstances.mHostInfo.mIP.CStr());

            for (auto& [_, instance] : hostInstances.mInstances) {
                IPs.push_back(instance.mIP.CStr());
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
    const String& networkID, const String& ip, const std::string& subnet,
    const std::vector<UnresolvedConnection>& unresolvedConnections)
{
    if (unresolvedConnections.empty()) {
        return;
    }

    for (const auto& unresolved : unresolvedConnections) {
        auto pending = std::make_unique<PendingConnection>();

        pending->mRequesterIdent  = requesterIdent;
        pending->mNodeID          = nodeID;
        pending->mNetworkID       = networkID;
        pending->mRequesterIP     = ip;
        pending->mRequesterSubnet = subnet.c_str();
        pending->mTargetItemID    = unresolved.mItemID.c_str();
        pending->mPort            = unresolved.mPort.c_str();
        pending->mProtocol        = unresolved.mProtocol.c_str();

        mPendingConnections.emplace(unresolved.mItemID, *pending);

        if (auto err = mStorage->AddPendingConnection(*pending); !err.IsNone()) {
            LOG_ERR() << "Failed to store pending connection" << Log::Field("instanceIdent", requesterIdent)
                      << Log::Field(err);
        } else {
            LOG_DBG() << "Stored pending connection" << Log::Field("requester", requesterIdent)
                      << Log::Field("targetItemID", unresolved.mItemID.c_str());
        }
    }
}

void NetworkManager::ReloadPendingConnections(const String& nodeID)
{
    auto pendingConnections = std::make_unique<StaticArray<PendingConnection, cMaxNumInstances * cMaxNumConnections>>();

    if (auto err = mStorage->GetAllPendingConnections(*pendingConnections); !err.IsNone()) {
        LOG_ERR() << "Failed to get pending connections from DB" << Log::Field(err);

        return;
    }

    for (const auto& pending : *pendingConnections) {
        if (pending.mNodeID != nodeID) {
            continue;
        }

        auto key   = pending.mTargetItemID.CStr();
        bool found = false;
        auto range = mPendingConnections.equal_range(key);

        for (auto it = range.first; it != range.second; ++it) {
            if (it->second == pending) {
                found = true;

                break;
            }
        }

        if (!found) {
            mPendingConnections.emplace(key, pending);
        }
    }
}

void NetworkManager::CleanConfirmedPendingConnections(
    const String& nodeID, const Array<InstanceNetworkStateInfo>& instances)
{
    auto dbPending = std::make_unique<StaticArray<PendingConnection, cMaxNumInstances * cMaxNumConnections>>();

    if (auto err = mStorage->GetAllPendingConnections(*dbPending); !err.IsNone()) {
        LOG_ERR() << "Failed to get pending connections for cleanup" << Log::Field(err);

        return;
    }

    for (const auto& pending : *dbPending) {
        if (pending.mNodeID != nodeID) {
            continue;
        }

        for (const auto& smInstance : instances) {
            if (smInstance.mInstanceIdent != pending.mRequesterIdent) {
                continue;
            }

            bool instanceFound = false;
            auto rule = GetInstanceRule(pending.mTargetItemID.CStr(), pending.mPort.CStr(), pending.mProtocol.CStr(),
                pending.mRequesterSubnet.CStr(), pending.mRequesterIP, instanceFound);

            if (!rule) {
                break;
            }

            auto confirmed = std::any_of(smInstance.mFirewallRules.begin(), smInstance.mFirewallRules.end(),
                [&](const auto& smRule) { return smRule == *rule; });

            if (confirmed) {
                if (auto err = mStorage->RemovePendingConnection(pending); !err.IsNone()) {
                    LOG_ERR() << "Failed to remove confirmed pending connection"
                              << Log::Field("instanceIdent", pending.mRequesterIdent) << Log::Field(err);
                }

                auto key   = pending.mTargetItemID.CStr();
                auto range = mPendingConnections.equal_range(key);

                for (auto it = range.first; it != range.second; ++it) {
                    if (it->second == pending) {
                        mPendingConnections.erase(it);

                        break;
                    }
                }
            }

            break;
        }
    }
}
void NetworkManager::ResolvePendingConnections(const InstanceIdent& newInstanceIdent)
{
    std::unordered_map<InstanceIdent, std::pair<std::string /*nodeID*/, aos::networkmanager::PendingFirewallUpdate>>
        updates;

    {
        std::lock_guard lock {mMutex};

        auto itemID = newInstanceIdent.mItemID.CStr();

        auto range = mPendingConnections.equal_range(itemID);
        if (range.first == range.second) {
            return;
        }

        for (auto it = range.first; it != range.second;) {
            const auto& pending = it->second;

            bool instanceFound = false;
            auto rule = GetInstanceRule(pending.mTargetItemID.CStr(), pending.mPort.CStr(), pending.mProtocol.CStr(),
                pending.mRequesterSubnet.CStr(), pending.mRequesterIP, instanceFound);

            if (rule) {
                auto& [nodeID, update] = updates[pending.mRequesterIdent];
                nodeID                 = pending.mNodeID.CStr();
                update.mInstanceIdent  = pending.mRequesterIdent;
                update.mFirewallRules.PushBack(*rule);

                // Remove from memory only; keep in DB until SM confirms via SyncNetworkState
                it = mPendingConnections.erase(it);
            } else {
                ++it;
            }
        }
    }

    if (mPendingUpdateHandler) {
        for (const auto& [_, updatePair] : updates) {
            const auto& [nodeID, update] = updatePair;

            LOG_DBG() << "Pushing pending firewall update" << Log::Field("instanceIdent", update.mInstanceIdent)
                      << Log::Field("nodeID", nodeID.c_str()) << Log::Field("rulesCount", update.mFirewallRules.Size());

            mPendingUpdateHandler->OnPendingFirewallUpdate(nodeID.c_str(), update);
        }
    }
}

} // namespace aos::cm::networkmanager
