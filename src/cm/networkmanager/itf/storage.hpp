/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_CM_NETWORKMANAGER_ITF_STORAGE_HPP_
#define AOS_CM_NETWORKMANAGER_ITF_STORAGE_HPP_

#include <string>
#include <unordered_map>
#include <vector>

#include <core/common/types/network.hpp>

#include <common/utils/utils.hpp>

namespace aos::cm::networkmanager {

/**
 * Exposed port.
 */
struct ExposedPort {
    StaticString<cProtocolNameLen> mProtocol;
    StaticString<cPortLen>         mPort;

    /**
     * Compares exposed ports.
     *
     * @param lhs exposed port.
     * @param rhs other exposed port.
     * @return bool.
     */
    friend bool operator==(const ExposedPort& lhs, const ExposedPort& rhs)
    {
        return lhs.mProtocol == rhs.mProtocol && lhs.mPort == rhs.mPort;
    }

    /**
     * Compares exposed ports.
     *
     * @param lhs exposed port.
     * @param rhs other exposed port.
     * @return bool.
     */
    friend bool operator!=(const ExposedPort& lhs, const ExposedPort& rhs) { return !(lhs == rhs); }
};

/**
 * Instance.
 */
struct Instance {
    StaticString<cIDLen>                                  mNetworkID;
    StaticString<cIDLen>                                  mNodeID;
    aos::InstanceIdent                                    mInstanceIdent;
    StaticString<cIPLen>                                  mIP;
    StaticArray<ExposedPort, cMaxNumExposedPorts>         mExposedPorts;
    StaticArray<StaticString<cIPLen>, cMaxNumDNSServers>  mDNSServers;
    StaticArray<StaticString<cHostNameLen>, cMaxNumHosts> mHosts;

    /**
     * Compares instances.
     *
     * @param lhs instance.
     * @param rhs other instance.
     * @return bool.
     */
    friend bool operator==(const Instance& lhs, const Instance& rhs)
    {
        return lhs.mNetworkID == rhs.mNetworkID && lhs.mNodeID == rhs.mNodeID
            && lhs.mInstanceIdent == rhs.mInstanceIdent && lhs.mIP == rhs.mIP && lhs.mExposedPorts == rhs.mExposedPorts
            && lhs.mDNSServers == rhs.mDNSServers && lhs.mHosts == rhs.mHosts;
    }

    /**
     * Compares instances.
     *
     * @param lhs instance.
     * @param rhs other instance.
     * @return bool.
     */
    friend bool operator!=(const Instance& lhs, const Instance& rhs) { return !(lhs == rhs); }
};

/**
 * Host.
 */
struct Host {
    StaticString<cIDLen> mNodeID;
    StaticString<cIPLen> mIP;

    /**
     * Compares hosts.
     *
     * @param lhs host.
     * @param rhs other host.
     * @return bool.
     */
    friend bool operator==(const Host& lhs, const Host& rhs)
    {
        return lhs.mNodeID == rhs.mNodeID && lhs.mIP == rhs.mIP;
    }

    /**
     * Compares hosts.
     *
     * @param lhs host.
     * @param rhs other host.
     * @return bool.
     */
    friend bool operator!=(const Host& lhs, const Host& rhs) { return !(lhs == rhs); }
};

/**
 * Host instances.
 */
struct HostInstances {
    Host                                        mHostInfo;
    std::unordered_map<InstanceIdent, Instance> mInstances;

    /**
     * Compares host instances.
     *
     * @param lhs host instances.
     * @param rhs other host instances.
     * @return bool.
     */
    friend bool operator==(const HostInstances& lhs, const HostInstances& rhs)
    {
        return lhs.mHostInfo == rhs.mHostInfo && lhs.mInstances == rhs.mInstances;
    }

    /**
     * Compares host instances.
     *
     * @param lhs host instances.
     * @param rhs other host instances.
     * @return bool.
     */
    friend bool operator!=(const HostInstances& lhs, const HostInstances& rhs) { return !(lhs == rhs); }
};

/**
 * Network.
 */
struct Network {
    StaticString<cIDLen>     mNetworkID;
    StaticString<cSubnetLen> mSubnet;
    uint64_t                 mVlanID;

    /**
     * Compares networks.
     *
     * @param lhs network.
     * @param rhs other network.
     * @return bool.
     */
    friend bool operator==(const Network& lhs, const Network& rhs)
    {
        return lhs.mNetworkID == rhs.mNetworkID && lhs.mSubnet == rhs.mSubnet && lhs.mVlanID == rhs.mVlanID;
    }

    /**
     * Compares networks.
     *
     * @param lhs network.
     * @param rhs other network.
     * @return bool.
     */
    friend bool operator!=(const Network& lhs, const Network& rhs) { return !(lhs == rhs); }
};

/**
 * Pending connection.
 */
struct PendingConnection {
    InstanceIdent                      mRequesterIdent;
    StaticString<cIDLen>               mNodeID;
    StaticString<cIDLen>               mNetworkID;
    StaticString<cIPLen>               mRequesterIP;
    StaticString<cSubnetLen>           mRequesterSubnet;
    StaticString<cConnectionTargetLen> mTarget;
    StaticString<cPortLen>             mPort;
    StaticString<cProtocolNameLen>     mProtocol;

    /**
     * Compares pending connections.
     *
     * @param lhs pending connection.
     * @param rhs other pending connection.
     * @return bool.
     */
    friend bool operator==(const PendingConnection& lhs, const PendingConnection& rhs)
    {
        return lhs.mRequesterIdent == rhs.mRequesterIdent && lhs.mNodeID == rhs.mNodeID
            && lhs.mNetworkID == rhs.mNetworkID && lhs.mRequesterIP == rhs.mRequesterIP
            && lhs.mRequesterSubnet == rhs.mRequesterSubnet && lhs.mTarget == rhs.mTarget && lhs.mPort == rhs.mPort
            && lhs.mProtocol == rhs.mProtocol;
    }

    /**
     * Compares pending connections.
     *
     * @param lhs pending connection.
     * @param rhs other pending connection.
     * @return bool.
     */
    friend bool operator!=(const PendingConnection& lhs, const PendingConnection& rhs) { return !(lhs == rhs); }
};

/**
 * Network state.
 */
struct NetworkState {
    Network                                        mNetwork;
    std::unordered_map<std::string, HostInstances> mHostInstances;

    /**
     * Compares network states.
     *
     * @param lhs network state.
     * @param rhs other network state.
     * @return bool.
     */
    friend bool operator==(const NetworkState& lhs, const NetworkState& rhs)
    {
        return lhs.mNetwork == rhs.mNetwork && lhs.mHostInstances == rhs.mHostInstances;
    }

    /**
     * Compares network states.
     *
     * @param lhs network state.
     * @param rhs other network state.
     * @return bool.
     */
    friend bool operator!=(const NetworkState& lhs, const NetworkState& rhs) { return !(lhs == rhs); }
};

/**
 * Storage interface.
 */
class StorageItf {
public:
    /**
     * Destructor.
     */
    virtual ~StorageItf() = default;

    /**
     * Adds network.
     *
     * @param network Network.
     * @return Error.
     */
    virtual Error AddNetwork(const Network& network) = 0;

    /**
     * Adds host.
     *
     * @param networkID Network ID.
     * @param host Host.
     * @return Error.
     */
    virtual Error AddHost(const String& networkID, const Host& host) = 0;

    /**
     * Adds instance.
     *
     * @param instance Instance.
     * @return Error.
     */
    virtual Error AddInstance(const Instance& instance) = 0;

    /**
     * Updates registered hostnames of an existing instance.
     *
     * @param instanceIdent Instance identifier.
     * @param hosts Registered hostnames. An empty array removes all hostnames.
     * @return Error.
     */
    virtual Error UpdateInstanceHosts(
        const InstanceIdent& instanceIdent, const Array<StaticString<cHostNameLen>>& hosts)
        = 0;

    /**
     * Gets networks.
     *
     * @param[out] networks Networks.
     * @return Error.
     */
    virtual Error GetNetworks(Array<Network>& networks) = 0;

    /**
     * Gets hosts.
     *
     * @param networkID Network ID.
     * @param[out] hosts Hosts.
     * @return Error.
     */
    virtual Error GetHosts(const String& networkID, Array<Host>& hosts) = 0;

    /**
     * Gets instances.
     *
     * @param networkID Network ID.
     * @param nodeID Node ID.
     * @param[out] instances Instances.
     * @return Error.
     */
    virtual Error GetInstances(const String& networkID, const String& nodeID, Array<Instance>& instances) = 0;

    /**
     * Removes network.
     *
     * @param networkID Network ID.
     * @return Error.
     */
    virtual Error RemoveNetwork(const String& networkID) = 0;

    /**
     * Removes host.
     *
     * @param networkID Network ID.
     * @param nodeID Node ID.
     * @return Error.
     */
    virtual Error RemoveHost(const String& networkID, const String& nodeID) = 0;

    /**
     * Removes instance.
     *
     * @param instanceIdent Instance identifier.
     * @return Error.
     */
    virtual Error RemoveNetworkInstance(const InstanceIdent& instanceIdent) = 0;

    /**
     * Adds pending connection.
     *
     * @param connection Pending connection.
     * @return Error.
     */
    virtual Error AddPendingConnection(const PendingConnection& connection) = 0;

    /**
     * Gets pending connections by target item ID or hostname.
     *
     * @param targetItemID Target item ID or hostname.
     * @param[out] connections Pending connections.
     * @return Error.
     */
    virtual Error GetPendingConnectionsByTarget(const String& targetItemID, Array<PendingConnection>& connections) = 0;

    /**
     * Gets all pending connections.
     *
     * @param[out] connections All pending connections.
     * @return Error.
     */
    virtual Error GetAllPendingConnections(Array<PendingConnection>& connections) = 0;

    /**
     * Removes a specific pending connection.
     *
     * @param connection Pending connection to remove.
     * @return Error.
     */
    virtual Error RemovePendingConnection(const PendingConnection& connection) = 0;

    /**
     * Removes all pending connections for a requester instance.
     *
     * @param requesterIdent Requester instance identifier.
     * @return Error.
     */
    virtual Error RemovePendingConnections(const InstanceIdent& requesterIdent) = 0;
};

} // namespace aos::cm::networkmanager

#endif // AOS_CM_NETWORKMANAGER_ITF_STORAGE_HPP_
