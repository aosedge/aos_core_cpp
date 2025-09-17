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

#include <common/utils/utils.hpp>
#include <core/common/types/types.hpp>

namespace aos::cm::networkmanager {

/**
 * Exposed port.
 */
struct ExposedPort {
    StaticString<cProtocolNameLen> mProtocol;
    StaticString<cPortLen>         mPort;
};

/**
 * Instance.
 */
struct Instance {
    StaticString<cIDLen>                                       mNetworkID;
    StaticString<cNodeIDLen>                                   mNodeID;
    aos::InstanceIdent                                         mInstanceIdent;
    StaticString<cIPLen>                                       mIP;
    StaticArray<ExposedPort, cMaxNumExposedPorts>              mExposedPorts;
    StaticArray<StaticString<cHostNameLen>, cMaxNumDNSServers> mDNSServers;
};

/**
 * Host.
 */
struct Host {
    StaticString<cNodeIDLen> mNodeID;
    StaticString<cIPLen>     mIP;
};

/**
 * Host instances.
 */
struct HostInstances {
    Host                                        mHostInfo;
    std::unordered_map<InstanceIdent, Instance> mInstances;
};

/**
 * Network.
 */
struct Network {
    StaticString<cIDLen>     mNetworkID;
    StaticString<cSubnetLen> mSubnet;
    uint64_t                 mVlanID;
};

/**
 * Network state.
 */
struct NetworkState {
    Network                                        mNetwork;
    std::unordered_map<std::string, HostInstances> mHostInstances;
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
    virtual Error RemoveInstance(const InstanceIdent& instanceIdent) = 0;
};

} // namespace aos::cm::networkmanager

#endif // AOS_CM_NETWORKMANAGER_ITF_STORAGE_HPP_
