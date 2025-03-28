/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef NETWORK_TRAFFICMONITOR_HPP
#define NETWORK_TRAFFICMONITOR_HPP

#include <chrono>
#include <shared_mutex>
#include <unordered_map>

#include <aos/common/tools/error.hpp>
#include <aos/common/tools/timer.hpp>
#include <aos/sm/networkmanager.hpp>

#include <network/iptables.hpp>
#include <utils/time.hpp>

namespace aos::sm::networkmanager {

class TrafficMonitor : public TrafficMonitorItf {
public:
    Error Init(StorageItf& storage, common::network::IPTablesItf& iptables, Duration updatePeriod = Time::cMinutes);

    /**
     * Starts traffic monitoring.
     *
     * @return Error.
     */
    Error Start() override;

    /**
     * Stops traffic monitoring.
     *
     * @return Error.
     */
    Error Stop() override;

    /**
     * Sets monitoring period.
     *
     * @param period monitoring period in seconds.
     */
    void SetPeriod(TrafficPeriod period) override;

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
        const String& instanceID, const String& IPAddress, uint64_t downloadLimit, uint64_t uploadLimit) override;

    /**
     * Stops monitoring instance.
     *
     * @param instanceID instance ID.
     * @return Error.
     */
    Error StopInstanceMonitoring(const String& instanceID) override;

    /**
     * Returns system traffic data.
     *
     * @param inputTraffic input traffic.
     * @param outputTraffic output traffic.
     * @return Error.
     */
    Error GetSystemData(uint64_t& inputTraffic, uint64_t& outputTraffic) const override;

    /**
     * Returns instance traffic data.
     *
     * @param instanceID instance ID.
     * @param inputTraffic input traffic.
     * @param outputTraffic output traffic.
     * @return Error.
     */
    Error GetInstanceTraffic(const String& instanceID, uint64_t& inputTraffic, uint64_t& outputTraffic) const override;

private:
    static constexpr auto cInSystemChain  = "AOS_SYSTEM_IN";
    static constexpr auto cOutSystemChain = "AOS_SYSTEM_OUT";
    constexpr static auto cSkipNetworks   = "127.0.0.0/8,10.0.0.0/8,192.168.0.0/16,172.16.0.0/12,172.17.0.0/16,"
                                            "172.18.0.0/16,172.19.0.0/16,172.20.0.0/14,172.24.0.0/14,172.28.0.0/14";

    struct TrafficData {
        bool        mDisabled {};
        std::string mAddresses;
        uint64_t    mCurrentValue {};
        uint64_t    mInitialValue {};
        uint64_t    mSubValue {};
        uint64_t    mLimit {};
        aos::Time   mLastUpdate {};
    };

    struct InstanceChains {
        std::string mInChain;
        std::string mOutChain;
    };

    Error DeleteAllTrafficChains();
    Error DeleteTrafficChain(const std::string& chain, const std::string& rootChain);
    Error DeleteChainRule(const std::string& chain, const common::network::RuleBuilder& builder);
    Error CreateTrafficChain(
        const std::string& chain, const std::string& rootChain, const std::string& addresses, uint64_t limit);
    Error UpdateTrafficData();
    Error GetTrafficChainBytes(const std::string& chain, uint64_t& bytes);
    bool  IsSamePeriod(TrafficPeriodEnum trafficPeriod, const aos::Time& t1, const aos::Time& t2) const;
    Error CheckTrafficLimit(const std::string& chain, TrafficData& trafficData);
    void  ResetTrafficData(TrafficData& trafficData, bool disable);
    Error SetChainState(const std::string& chain, const std::string& addresses, bool enable);
    Error GetTrafficData(
        const std::string& inChain, const std::string& outChain, uint64_t& inputTraffic, uint64_t& outputTraffic) const;

    StorageItf*                                     mStorage {};
    common::network::IPTablesItf*                   mIPTables {};
    std::unordered_map<std::string, TrafficData>    mTrafficData {};
    std::unordered_map<std::string, InstanceChains> mInstanceChains {};
    mutable std::shared_mutex                       mMutex {};
    aos::Timer                                      mTimer {};
    TrafficPeriod                                   mTrafficPeriod {};
    Duration                                        mUpdatePeriod {};
    bool                                            mStop {};
};
} // namespace aos::sm::networkmanager
#endif // NETWORK_TRAFFICMONITOR_HPP
