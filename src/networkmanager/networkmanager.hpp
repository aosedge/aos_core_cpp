/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef NETWORKMANAGER_HPP_
#define NETWORKMANAGER_HPP_

#include <aos/sm/networkmanager.hpp>

namespace aos::sm::networkmanager {

/**
 * Namespace manager instance.
 */
class NamespaceManager : public NamespaceManagerItf {
public:
    /**
     * Creates network namespace.
     * @param instanceID instance ID.
     * @return Error.
     */
    Error CreateNetworkNamespace(const String& instanceID) override
    {
        (void)instanceID;

        return ErrorEnum::eNone;
    }

    /**
     * Returns network namespace path.
     *
     * @param instanceID instance ID.
     * @return RetWithError<StaticString<cFilePathLen>>.
     */
    RetWithError<StaticString<cFilePathLen>> GetNetworkNamespacePath(const String& instanceID) const override
    {
        (void)instanceID;

        return {""};
    }

    /**
     * Deletes network namespace.
     *
     * @param instanceID instance ID.
     * @return Error.
     */
    Error DeleteNetworkNamespace(const String& instanceID) override
    {
        (void)instanceID;

        return ErrorEnum::eNone;
    }
};

/**
 * Network manager provider interface.
 */
class NetworkInterfaceManager : public NetworkInterfaceManagerItf {
public:
    /**
     * Removes interface.
     *
     * @param ifname interface name.
     * @return Error.
     */
    Error RemoveInterface(const String& ifname) override
    {
        (void)ifname;

        return ErrorEnum::eNone;
    }
};

/**
 * Traffic monitor interface.
 */
class TrafficMonitor : public TrafficMonitorItf {
public:
    /**
     * Starts traffic monitoring.
     *
     * @return Error.
     */
    Error Start() override { return ErrorEnum::eNone; }

    /**
     * Stops traffic monitoring.
     *
     * @return Error.
     */
    Error Stop() override { return ErrorEnum::eNone; }

    /**
     * Sets monitoring period.
     *
     * @param period monitoring period in seconds.
     */
    void SetPeriod(TrafficPeriod period) override { (void)period; }

    /**
     * Starts monitoring instance.
     *
     * @param instanceID instance ID.
     * @param IPAddress instance IP address.
     * @param downloadLimit download limit.
     * @param uploadLimit upload limit.
     * @return Error.
     */
    Error StartInstanceMonitoring(
        const String& instanceID, const String& IPAddress, uint64_t downloadLimit, uint64_t uploadLimit) override
    {
        (void)instanceID;
        (void)IPAddress;
        (void)downloadLimit;
        (void)uploadLimit;

        return ErrorEnum::eNone;
    }

    /**
     * Stops monitoring instance.
     *
     * @param instanceID instance ID.
     * @return Error.
     */
    Error StopInstanceMonitoring(const String& instanceID) override
    {
        (void)instanceID;

        return ErrorEnum::eNone;
    }

    /**
     * Returns system traffic data.
     *
     * @param inputTraffic input traffic.
     * @param outputTraffic output traffic.
     * @return Error.
     */
    Error GetSystemData(uint64_t& inputTraffic, uint64_t& outputTraffic) const override
    {
        (void)inputTraffic;
        (void)outputTraffic;

        return ErrorEnum::eNone;
    }

    /**
     * Returns instance traffic data.
     *
     * @param instanceID instance ID.
     * @param inputTraffic input traffic.
     * @param outputTraffic output traffic.
     * @return Error.
     */
    Error GetInstanceTraffic(const String& instanceID, uint64_t& inputTraffic, uint64_t& outputTraffic) const override
    {
        (void)instanceID;
        (void)inputTraffic;
        (void)outputTraffic;

        return ErrorEnum::eNone;
    }
};

} // namespace aos::sm::networkmanager

#endif
