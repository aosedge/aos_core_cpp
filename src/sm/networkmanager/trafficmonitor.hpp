/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_NETWORKMANAGER_TRAFFICMONITOR_HPP_
#define AOS_SM_NETWORKMANAGER_TRAFFICMONITOR_HPP_

#include <chrono>
#include <memory>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <core/common/tools/error.hpp>
#include <core/common/tools/timer.hpp>
#include <core/sm/networkmanager/itf/storage.hpp>
#include <core/sm/networkmanager/itf/trafficmonitor.hpp>

#include <common/utils/time.hpp>
#include <sm/nftables/itf/firewallbackend.hpp>

namespace aos::sm::networkmanager {

class TrafficMonitor : public TrafficMonitorItf {
public:
    Error Init(StorageItf& storage, nftables::FWBackendItf& backend, Duration updatePeriod = Time::cMinutes);

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
    Error GetSystemTraffic(uint64_t& inputTraffic, uint64_t& outputTraffic) const override;

    /**
     * Returns instance traffic data.
     *
     * @param instanceID instance ID.
     * @param inputTraffic input traffic.
     * @param outputTraffic output traffic.
     * @return Error.
     */
    Error GetInstanceTraffic(const String& instanceID, uint64_t& inputTraffic, uint64_t& outputTraffic) const override;

    /**
     * Begins batch mode: StartInstanceMonitoring/StopInstanceMonitoring stage
     * their nft operations into a single shared transaction instead of
     * committing per instance.
     *
     * @return Error.
     */
    Error BeginBatch() override;

    /**
     * Commits the staged batch in one nft transaction, records the handles it
     * added and leaves batch mode.
     *
     * @return Error.
     */
    Error FlushBatch() override;

    /**
     * Discards the staged batch and leaves batch mode without applying anything,
     * dropping the monitoring state the batch staged.
     *
     * @return Error.
     */
    Error AbortBatch() override;

    /**
     * Deletes by handle everything the last flushed batch added, together with
     * the counter chains it created, and drops their monitoring state.
     *
     * @return Error.
     */
    Error Revert() override;

private:
    static constexpr auto cTable          = "aos-traffic";
    static constexpr auto cInputChain     = "input";
    static constexpr auto cOutputChain    = "output";
    static constexpr auto cForwardChain   = "forward";
    static constexpr auto cInSystemChain  = "in_system";
    static constexpr auto cOutSystemChain = "out_system";
    static constexpr auto cInChainPrefix  = "in_";
    static constexpr auto cOutChainPrefix = "out_";

    struct TrafficData {
        bool        mDisabled {};
        std::string mAddress;
        uint64_t    mCurrentValue {};
        uint64_t    mInitialValue {};
        uint64_t    mSubValue {};
        uint64_t    mLimit {};
        aos::Time   mLastUpdate {};
    };

    struct InstanceChains {
        std::string            mIP;
        std::string            mInChain;
        std::string            mOutChain;
        nftables::FWRuleHandle mInHandle {};
        nftables::FWRuleHandle mOutHandle {};
    };

    using StagedTrafficData = std::vector<std::pair<std::string, TrafficData>>;

    Error CreateSystemChains();
    Error DeleteTrafficTable();
    Error CreateInstanceChain(nftables::FWTxnItf& txn, const std::string& chain, bool isInChain,
        const std::string& address, const std::string& parentBaseChain, uint64_t limit, StagedTrafficData& staged);
    Error BuildInstanceMonitoring(nftables::FWTxnItf& txn, const InstanceChains& chains, StagedTrafficData& staged,
        uint64_t downloadLimit, uint64_t uploadLimit);
    void  DeleteInstanceMonitoring(
         nftables::FWTxnItf& txn, const InstanceChains& chains, const std::vector<nftables::FWRuleHandle>& jumpHandles);
    void  DropBatchInstanceState(const std::vector<std::string>& instanceIDs);
    void  PublishTrafficData(StagedTrafficData& staged);
    Error AppendChainCounterRules(
        nftables::FWTxnItf& txn, const std::string& chain, bool isInChain, const std::string& address, bool disabled);
    Error UpdateTrafficData();
    Error GetTrafficChainBytes(const std::string& chain, uint64_t& bytes);
    bool  IsSamePeriod(TrafficPeriodEnum trafficPeriod, const aos::Time& t1, const aos::Time& t2) const;
    Error CheckTrafficLimit(const std::string& chain, TrafficData& trafficData);
    void  ResetTrafficData(TrafficData& trafficData, bool disable);
    Error SetChainState(const std::string& chain, const std::string& address, bool enable);
    Error GetTrafficData(
        const std::string& inChain, const std::string& outChain, uint64_t& inputTraffic, uint64_t& outputTraffic) const;

    StorageItf*                                     mStorage {};
    nftables::FWBackendItf*                         mBackend {};
    std::unordered_map<std::string, TrafficData>    mTrafficData {};
    std::unordered_map<std::string, InstanceChains> mInstanceChains {};
    mutable std::shared_mutex                       mMutex {};

    std::mutex                          mBatchMutex;
    bool                                mBatchMode {false};
    std::unique_ptr<nftables::FWTxnItf> mBatchTxn;
    std::vector<std::string>            mBatchInstances;
    std::set<nftables::FWRuleHandle>    mAppliedHandles;

    aos::Timer    mTimer {};
    TrafficPeriod mTrafficPeriod {};
    Duration      mUpdatePeriod {};
    bool          mStop {};
};

} // namespace aos::sm::networkmanager

#endif // NETWORK_TRAFFICMONITOR_HPP
