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
     * @param rhs other exposed port.
     * @return bool.
     */
    bool operator==(const ExposedPort& rhs) const { return mProtocol == rhs.mProtocol && mPort == rhs.mPort; }

    /**
     * Compares exposed ports.
     *
     * @param rhs other exposed port.
     * @return bool.
     */
    bool operator!=(const ExposedPort& rhs) const { return !(*this == rhs); }
};

/**
 * Instance.
 */
struct Instance {
    StaticString<cIDLen>                                 mNetworkID;
    StaticString<cIDLen>                                 mNodeID;
    aos::InstanceIdent                                   mInstanceIdent;
    StaticString<cIPLen>                                 mIP;
    StaticArray<ExposedPort, cMaxNumExposedPorts>        mExposedPorts;
    StaticArray<StaticString<cIPLen>, cMaxNumDNSServers> mDNSServers;

    /**
     * Compares instances.
     *
     * @param rhs other instance.
     * @return bool.
     */
    bool operator==(const Instance& rhs) const
    {
        return mNetworkID == rhs.mNetworkID && mNodeID == rhs.mNodeID && mInstanceIdent == rhs.mInstanceIdent
            && mIP == rhs.mIP && mExposedPorts == rhs.mExposedPorts && mDNSServers == rhs.mDNSServers;
    }

    /**
     * Compares instances.
     *
     * @param rhs other instance.
     * @return bool.
     */
    bool operator!=(const Instance& rhs) const { return !(*this == rhs); }
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
     * @param rhs other host.
     * @return bool.
     */
    bool operator==(const Host& rhs) const { return mNodeID == rhs.mNodeID && mIP == rhs.mIP; }

    /**
     * Compares hosts.
     *
     * @param rhs other host.
     * @return bool.
     */
    bool operator!=(const Host& rhs) const { return !(*this == rhs); }
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
     * @param rhs other host instances.
     * @return bool.
     */
    bool operator==(const HostInstances& rhs) const
    {
        return mHostInfo == rhs.mHostInfo && mInstances == rhs.mInstances;
    }

    /**
     * Compares host instances.
     *
     * @param rhs other host instances.
     * @return bool.
     */
    bool operator!=(const HostInstances& rhs) const { return !(*this == rhs); }
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
     * @param rhs other network.
     * @return bool.
     */
    bool operator==(const Network& rhs) const
    {
        return mNetworkID == rhs.mNetworkID && mSubnet == rhs.mSubnet && mVlanID == rhs.mVlanID;
    }

    /**
     * Compares networks.
     *
     * @param rhs other network.
     * @return bool.
     */
    bool operator!=(const Network& rhs) const { return !(*this == rhs); }
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
     * @param rhs other network state.
     * @return bool.
     */
    bool operator==(const NetworkState& rhs) const
    {
        return mNetwork == rhs.mNetwork && mHostInstances == rhs.mHostInstances;
    }

    /**
     * Compares network states.
     *
     * @param rhs other network state.
     * @return bool.
     */
    bool operator!=(const NetworkState& rhs) const { return !(*this == rhs); }
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
};

} // namespace aos::cm::networkmanager

#endif // AOS_CM_NETWORKMANAGER_ITF_STORAGE_HPP_
