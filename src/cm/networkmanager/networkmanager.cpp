/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <algorithm>

#include <common/network/utils.hpp>
#include <common/utils/exception.hpp>
#include <sm/logger/logmodule.hpp>

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

Error NetworkManager::Init(StorageItf& storage, crypto::RandomItf& random, SenderItf& sender, DNSServerItf& dnsServer)
{
    mStorage   = &storage;
    mRandom    = &random;
    mSender    = &sender;
    mDNSServer = &dnsServer;

    mIpSubnet.Init();

    auto networks = std::make_unique<StaticArray<Network, cMaxNumOwners>>();

    if (auto err = mStorage->GetNetworks(*networks); !err.IsNone()) {
        return err;
    }

    for (const auto& network : *networks) {
        NetworkState networkState;
        networkState.mNetwork = network;

        auto hosts = std::make_unique<StaticArray<Host, cMaxNumNodes * cMaxNumOwners>>();

        if (auto err = mStorage->GetHosts(network.mNetworkID, *hosts); !err.IsNone()) {
            return err;
        }

        for (const auto& host : *hosts) {
            HostInstances hostInstances;
            hostInstances.mHostInfo = host;

            auto instances = std::make_unique<StaticArray<Instance, cMaxNumInstances>>();

            if (auto err = mStorage->GetInstances(network.mNetworkID, host.mNodeID, *instances); !err.IsNone()) {
                return err;
            }

            for (const auto& instance : *instances) {
                hostInstances.mInstances.emplace(instance.mInstanceIdent, instance);
            }

            networkState.mHostInstances.emplace(host.mNodeID.CStr(), std::move(hostInstances));
        }

        mNetworkStates.emplace(network.mNetworkID.CStr(), std::move(networkState));
    }

    RemoveExistedNetworks();

    return ErrorEnum::eNone;
}

Error NetworkManager::GetInstances(Array<InstanceIdent>& instances) const
{
    for (const auto& [_, networkState] : mNetworkStates) {
        for (const auto& [__, hostInstances] : networkState.mHostInstances) {
            for (const auto& [instanceIdent, ___] : hostInstances.mInstances) {
                if (auto err = instances.PushBack(instanceIdent); !err.IsNone()) {
                    return err;
                }
            }
        }
    }

    return ErrorEnum::eNone;
}

Error NetworkManager::RemoveInstanceNetworkParameters(const InstanceIdent& instanceIdent, const String& nodeID)
{
    LOG_DBG() << "Removing instance network parameters" << Log::Field("instanceIdent", instanceIdent);

    try {
        for (auto& [networkID, networkState] : mNetworkStates) {
            auto itHost = networkState.mHostInstances.find(nodeID.CStr());
            if (itHost == networkState.mHostInstances.end()) {
                continue;
            }

            auto itInstance = itHost->second.mInstances.find(instanceIdent);
            if (itInstance == itHost->second.mInstances.end()) {
                LOG_WRN() << "Instance network parameters not found" << Log::Field("instanceIdent", instanceIdent)
                          << Log::Field("nodeID", nodeID);

                return ErrorEnum::eNone;
            }

            RemoveInstanceNetwork(networkID, itInstance->second.mIP.CStr(), instanceIdent);
            itHost->second.mInstances.erase(itInstance);

            LOG_DBG() << "Removed instance network parameters" << Log::Field("networkID", networkID.c_str())
                      << Log::Field("instanceIdent", instanceIdent);
        }
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    LOG_WRN() << "Instance network parameters not found" << Log::Field("instanceIdent", instanceIdent);

    return ErrorEnum::eNone;
}

Error NetworkManager::UpdateProviderNetwork(const Array<StaticString<cIDLen>>& providers, const String& nodeID)
{
    LOG_DBG() << "Updating provider network" << Log::Field("nodeID", nodeID);

    std::vector<NetworkParameters> networkParametersList;

    try {
        RemoveProviderNetworks(providers, nodeID);

        for (const auto& provider : providers) {
            std::unique_ptr<NetworkParameters> networkParameters = std::make_unique<NetworkParameters>();

            AddProviderNetwork(provider, nodeID, *networkParameters);

            networkParametersList.push_back(*networkParameters);
        }
    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    LOG_DBG() << "Updated provider network" << Log::Field("nodeID", nodeID);

    return mSender->SendNetwork(nodeID.CStr(), networkParametersList);
}

Error NetworkManager::PrepareInstanceNetworkParameters(const InstanceIdent& instanceIdent, const String& networkID,
    const String& nodeID, const NetworkServiceData& networkData, NetworkParameters& result)
{
    LOG_DBG() << "Preparing instance network parameters" << Log::Field("instanceIdent", instanceIdent)
              << Log::Field("networkID", networkID);

    std::vector<std::string> hosts;

    std::transform(networkData.mHosts.begin(), networkData.mHosts.end(), std::back_inserter(hosts),
        [](const auto& host) { return host.CStr(); });

    PrepareInstanceIdentHosts(instanceIdent, networkID, hosts);

    try {
        auto it = mNetworkStates.find(networkID.CStr());
        if (it == mNetworkStates.end()) {
            return Error(ErrorEnum::eRuntime, "network not found");
        }

        auto itHost = it->second.mHostInstances.find(nodeID.CStr());
        if (itHost == it->second.mHostInstances.end()) {
            return Error(ErrorEnum::eRuntime, "host not found");
        }

        result.mNetworkID = networkID;
        result.mSubnet    = it->second.mNetwork.mSubnet;
        result.mVlanID    = it->second.mNetwork.mVlanID;

        if (auto itInstance = itHost->second.mInstances.find(instanceIdent);
            itInstance != itHost->second.mInstances.end()) {
            result.mIP         = itInstance->second.mIP;
            result.mDNSServers = itInstance->second.mDNSServers;

            if (auto err = PrepareFirewallRules(it->second.mNetwork.mSubnet.CStr(), itInstance->second.mIP,
                    networkData.mAllowedConnections, result);
                !err.IsNone()) {
                return err;
            }

            return AddHosts(hosts, itInstance->second.mIP.CStr());
        }

        auto IP    = mIpSubnet.GetAvailableIP(networkID.CStr());
        auto dnsIP = mDNSServer->GetIP();

        result.mIP = IP.c_str();
        result.mDNSServers.PushBack(dnsIP.c_str());

        Instance instance;
        instance.mNetworkID     = networkID;
        instance.mNodeID        = nodeID;
        instance.mInstanceIdent = instanceIdent;
        instance.mIP            = IP.c_str();
        instance.mDNSServers.PushBack(dnsIP.c_str());

        if (auto err = ParseExposedPorts(networkData.mExposedPorts, instance); !err.IsNone()) {
            return err;
        }

        itHost->second.mInstances.emplace(instanceIdent, instance);

        if (auto err = PrepareFirewallRules(
                it->second.mNetwork.mSubnet.CStr(), IP.c_str(), networkData.mAllowedConnections, result);
            !err.IsNone()) {
            return err;
        }

        if (auto err = AddHosts(hosts, IP); !err.IsNone()) {
            return err;
        }

        if (auto err = mStorage->AddInstance(instance); !err.IsNone()) {
            return err;
        }

        LOG_DBG() << "Prepared instance network parameters" << Log::Field("networkID", networkID)
                  << Log::Field("nodeID", nodeID) << Log::Field("instanceIdent", instanceIdent)
                  << Log::Field("IP", result.mIP);

    } catch (const std::exception& e) {
        return common::utils::ToAosError(e);
    }

    return ErrorEnum::eNone;
}

Error NetworkManager::RestartDNSServer()
{
    if (auto err = mDNSServer->UpdateHostsFile(mHosts); !err.IsNone()) {
        return err;
    }

    mHosts.clear();

    return mDNSServer->Restart();
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

Error NetworkManager::ParseExposedPorts(const Array<StaticString<cExposedPortLen>>& exposedPorts, Instance& instance)
{
    for (const auto& exposedPort : exposedPorts) {
        StaticArray<StaticString<cExposedPortLen>, cExposedPortConfigExpectedLen> portConfig;

        if (auto err = exposedPort.Split(portConfig, '/'); !err.IsNone()) {
            return err;
        }

        if (portConfig.Size() == 0) {
            return Error(ErrorEnum::eRuntime, "unsupported ExposedPorts format");
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

FirewallRule NetworkManager::GetInstanceRule(const std::string& itemID, const std::string& port,
    const std::string& protocol, const std::string& subnet, const String& ip)
{
    for (auto& [_, networkState] : mNetworkStates) {
        for (auto& [nodeID, hostInstances] : networkState.mHostInstances) {
            for (auto& [instanceID, instance] : hostInstances.mInstances) {
                if (instance.mInstanceIdent.mItemID != itemID.c_str()) {
                    continue;
                }

                // instance is in the same subnet could be connected without firewall rules
                if (common::network::NetworkContainsIP(subnet, instance.mIP.CStr())) {
                    continue;
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

    throw std::runtime_error("rule not found");
}

Error NetworkManager::PrepareFirewallRules(const std::string& subnet, const String& ip,
    const Array<StaticString<cConnectionNameLen>>& allowedConnections, NetworkParameters& result)
{
    if (allowedConnections.IsEmpty()) {
        return ErrorEnum::eNone;
    }

    try {
        for (const auto& connection : allowedConnections) {
            std::string itemID, port, protocol;
            ParseAllowConnection(connection, itemID, port, protocol);
            auto rule = GetInstanceRule(itemID, port, protocol, subnet, ip);
            result.mFirewallRules.PushBack(rule);
        }
    } catch (const std::exception& e) {
        return Error(aos::ErrorEnum::eRuntime, e.what());
    }

    return ErrorEnum::eNone;
}

Error NetworkManager::AddHosts(const std::vector<std::string>& hosts, const std::string& ip)
{
    for (const auto& host : hosts) {
        if (IsHostExist(host)) {
            return Error(ErrorEnum::eAlreadyExist, "host already exists");
        }

        mHosts[ip].push_back(host);
    }

    return ErrorEnum::eNone;
}

void NetworkManager::PrepareInstanceIdentHosts(
    const InstanceIdent& instanceIdent, const String& networkID, std::vector<std::string>& hosts) const
{
    if (instanceIdent.mItemID.IsEmpty() || instanceIdent.mSubjectID.IsEmpty()) {
        return;
    }

    StaticString<cHostNameLen> hostName;

    hostName.Format("%d.%s.%s", instanceIdent.mInstance, instanceIdent.mSubjectID.CStr(), instanceIdent.mItemID.CStr());
    hosts.push_back(hostName.CStr());

    hostName.Format("%d.%s.%s.%s", instanceIdent.mInstance, instanceIdent.mSubjectID.CStr(),
        instanceIdent.mItemID.CStr(), networkID.CStr());
    hosts.push_back(hostName.CStr());

    if (instanceIdent.mInstance == 0) {
        hostName.Format("%s.%s", instanceIdent.mSubjectID.CStr(), instanceIdent.mItemID.CStr());
        hosts.push_back(hostName.CStr());
    }

    hostName.Format("%s.%s.%s", instanceIdent.mSubjectID.CStr(), instanceIdent.mItemID.CStr(), networkID.CStr());
    hosts.push_back(hostName.CStr());
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

void NetworkManager::AddProviderNetwork(
    const String& networkID, const String& nodeID, NetworkParameters& networkParameters)
{
    LOG_DBG() << "Adding provider network" << Log::Field("networkID", networkID) << Log::Field("nodeID", nodeID);

    networkParameters.mNetworkID = networkID;

    auto it = mNetworkStates.find(networkID.CStr());
    if (it != mNetworkStates.end()) {
        auto itHost = it->second.mHostInstances.find(nodeID.CStr());
        if (itHost != it->second.mHostInstances.end()) {
            networkParameters.mSubnet = it->second.mNetwork.mSubnet;
            networkParameters.mVlanID = it->second.mNetwork.mVlanID;
            networkParameters.mIP     = itHost->second.mHostInfo.mIP;

            return;
        }

        auto IP = mIpSubnet.GetAvailableIP(networkID.CStr());

        networkParameters.mSubnet = it->second.mNetwork.mSubnet;
        networkParameters.mVlanID = it->second.mNetwork.mVlanID;
        networkParameters.mIP     = IP.c_str();

        Host host;
        host.mNodeID = nodeID;
        host.mIP     = IP.c_str();

        HostInstances hostInstances;
        hostInstances.mHostInfo = host;

        it->second.mHostInstances.emplace(nodeID.CStr(), std::move(hostInstances));

        auto err = mStorage->AddHost(networkID, host);
        AOS_ERROR_CHECK_AND_THROW(err, "error adding host");

        return;
    }

    CreateProviderNetwork(networkID, nodeID, networkParameters);

    LOG_DBG() << "Added provider network" << Log::Field("networkID", networkID) << Log::Field("nodeID", nodeID);
}

void NetworkManager::CreateProviderNetwork(
    const String& networkID, const String& nodeID, NetworkParameters& networkParameters)
{
    LOG_DBG() << "Creating provider network" << Log::Field("networkID", networkID) << Log::Field("nodeID", nodeID);

    networkParameters.mVlanID = GenerateVlanID();

    auto subnet = mIpSubnet.GetAvailableSubnet(networkID.CStr());

    networkParameters.mSubnet = subnet.c_str();

    auto IP = mIpSubnet.GetAvailableIP(networkID.CStr());

    networkParameters.mIP = IP.c_str();

    Network network;
    network.mNetworkID = networkID;
    network.mSubnet    = subnet.c_str();
    network.mVlanID    = networkParameters.mVlanID;

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

    LOG_DBG() << "Created provider network" << Log::Field("networkID", networkID) << Log::Field("nodeID", nodeID);
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

void NetworkManager::RemoveProviderNetworks(const Array<StaticString<cIDLen>>& providers, const String& nodeID)
{
    LOG_DBG() << "Remove provider networks" << Log::Field("nodeID", nodeID);

    for (auto it = mNetworkStates.begin(); it != mNetworkStates.end();) {
        if (ShouldRemoveNetwork(it->second, providers)) {
            CleanupHostFromNetwork(it->second, nodeID);

            if (IsNetworkEmpty(it->second)) {
                CleanupEmptyNetwork(it->second);
                it = mNetworkStates.erase(it);

                continue;
            }
        }

        ++it;
    }
}

bool NetworkManager::ShouldRemoveNetwork(
    const NetworkState& networkState, const Array<StaticString<cIDLen>>& providers) const
{
    return providers.Find(networkState.mNetwork.mNetworkID.CStr()) == providers.end();
}

void NetworkManager::CleanupHostFromNetwork(NetworkState& networkState, const String& nodeID)
{
    auto itHost = networkState.mHostInstances.find(nodeID.CStr());
    if (itHost == networkState.mHostInstances.end()) {
        return;
    }

    auto& hostInstances = itHost->second;

    if (auto err = mStorage->RemoveHost(networkState.mNetwork.mNetworkID, nodeID); !err.IsNone()) {
        LOG_ERR() << "Error removing host" << Log::Field("err", err);
    }

    for (auto& [instanceID, instance] : hostInstances.mInstances) {
        RemoveInstanceNetwork(networkState.mNetwork.mNetworkID.CStr(), instance.mIP.CStr(), instanceID);
    }

    networkState.mHostInstances.erase(itHost);

    LOG_DBG() << "Remove host from network" << Log::Field("networkID", networkState.mNetwork.mNetworkID.CStr())
              << Log::Field("nodeID", nodeID);
}

bool NetworkManager::IsNetworkEmpty(const NetworkState& networkState) const
{
    return networkState.mHostInstances.empty();
}

void NetworkManager::CleanupEmptyNetwork(const NetworkState& networkState)
{
    mIpSubnet.ReleaseIPNetPool(networkState.mNetwork.mNetworkID.CStr());

    if (auto err = mStorage->RemoveNetwork(networkState.mNetwork.mNetworkID); !err.IsNone()) {
        LOG_ERR() << "Error removing network" << Log::Field("err", err);
    }

    LOG_DBG() << "Remove empty network" << Log::Field("networkID", networkState.mNetwork.mNetworkID.CStr());
}

void NetworkManager::RemoveInstanceNetwork(
    const std::string& networkID, const std::string& IP, const InstanceIdent& instanceIdent)
{
    mIpSubnet.ReleaseIPToSubnet(networkID, IP);
    mHosts.erase(IP);

    auto err = mStorage->RemoveInstance(instanceIdent);
    AOS_ERROR_CHECK_AND_THROW(err, "error removing instance");
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

} // namespace aos::cm::networkmanager
