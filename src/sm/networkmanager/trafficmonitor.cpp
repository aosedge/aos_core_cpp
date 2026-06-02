/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <algorithm>
#include <array>
#include <cctype>
#include <mutex>

#include <core/common/tools/logger.hpp>

#include "trafficmonitor.hpp"

namespace aos::sm::networkmanager {

/***********************************************************************************************************************
 * Static
 **********************************************************************************************************************/

namespace {

const std::array<const char*, 10> cSkipNetworks {"127.0.0.0/8", "10.0.0.0/8", "192.168.0.0/16", "172.16.0.0/12",
    "172.17.0.0/16", "172.18.0.0/16", "172.19.0.0/16", "172.20.0.0/14", "172.24.0.0/14", "172.28.0.0/14"};

bool HasPrefix(const std::string& s, const std::string& p)
{
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

std::string SanitiseInstanceID(const String& instanceID)
{
    std::string out;

    out.reserve(instanceID.Size());

    for (size_t i = 0; i < instanceID.Size(); ++i) {
        const auto c = instanceID[i];

        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
            out += c;
        } else {
            out += '_';
        }
    }

    return out;
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error TrafficMonitor::Init(StorageItf& storage, nftables::FWBackendItf& backend, Duration updatePeriod)
{
    LOG_DBG() << "Init traffic monitor";

    mStorage       = &storage;
    mBackend       = &backend;
    mTrafficPeriod = TrafficPeriodEnum::eDayPeriod;
    mUpdatePeriod  = updatePeriod;

    if (auto err = DeleteTrafficTable(); !err.IsNone()) {
        LOG_DBG() << "Stale traffic table cleanup skipped" << Log::Field(err);
    }

    return CreateSystemChains();
}

Error TrafficMonitor::Start()
{
    {
        std::unique_lock lock {mMutex};

        LOG_DBG() << "Start traffic monitor";

        mStop = false;
    }

    return mTimer.Start(
        mUpdatePeriod,
        [this](void*) {
            if (auto err = UpdateTrafficData(); err != ErrorEnum::eNone) {
                LOG_ERR() << "Can't update traffic data" << Log::Field(err);
            }
        },
        false);
}

Error TrafficMonitor::Stop()
{
    {
        std::unique_lock lock {mMutex};

        LOG_DBG() << "Stop traffic monitor";

        mStop = true;
    }

    if (auto err = mTimer.Stop(); !err.IsNone()) {
        LOG_ERR() << "Can't stop timer" << Log::Field(err);
    }

    if (auto err = DeleteTrafficTable(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    {
        std::unique_lock lock {mMutex};

        mTrafficData.clear();
        mInstanceChains.clear();
    }

    return ErrorEnum::eNone;
}

void TrafficMonitor::SetPeriod(TrafficPeriod period)
{
    std::unique_lock lock {mMutex};

    LOG_DBG() << "Set traffic period" << Log::Field("period", static_cast<int>(period));

    mTrafficPeriod = period;
}

Error TrafficMonitor::StartInstanceMonitoring(
    const String& instanceID, const String& IPAddress, uint64_t downloadLimit, uint64_t uploadLimit)
{
    if (IPAddress.IsEmpty() || instanceID.IsEmpty()) {
        return ErrorEnum::eNone;
    }

    {
        std::shared_lock lock {mMutex};

        LOG_DBG() << "Start instance monitoring" << Log::Field("instanceID", instanceID);

        if (mInstanceChains.find(instanceID.CStr()) != mInstanceChains.end()) {
            return ErrorEnum::eNone;
        }
    }

    const auto safeID = SanitiseInstanceID(instanceID);

    InstanceChains chains {
        IPAddress.CStr(),
        std::string {cInChainPrefix} + safeID,
        std::string {cOutChainPrefix} + safeID,
    };

    auto txn = mBackend->NewTxn();

    StagedTrafficData staged;

    if (auto err = CreateInstanceChain(*txn, chains.mInChain, true, chains.mIP, cForwardChain, downloadLimit, staged);
        !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = CreateInstanceChain(*txn, chains.mOutChain, false, chains.mIP, cForwardChain, uploadLimit, staged);
        !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = txn->Commit(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    PublishTrafficData(staged);

    {
        std::unique_lock lock {mMutex};

        mInstanceChains[instanceID.CStr()] = std::move(chains);
    }

    return ErrorEnum::eNone;
}

Error TrafficMonitor::StopInstanceMonitoring(const String& instanceID)
{
    if (instanceID.IsEmpty()) {
        return ErrorEnum::eNone;
    }

    InstanceChains chains;

    {
        std::shared_lock lock {mMutex};

        LOG_DBG() << "Stop instance monitoring" << Log::Field("instanceID", instanceID);

        auto it = mInstanceChains.find(instanceID.CStr());
        if (it == mInstanceChains.end()) {
            return ErrorEnum::eNone;
        }

        chains = it->second;
    }

    std::vector<nftables::FWListedRule> forwardRules;

    if (auto err = mBackend->ListChainRules(cTable, cForwardChain, forwardRules); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    auto txn = mBackend->NewTxn();

    for (const auto& r : forwardRules) {
        if (r.mRule.mAction == nftables::FWActionEnum::eJump
            && (r.mRule.mJumpTarget == chains.mInChain || r.mRule.mJumpTarget == chains.mOutChain)) {
            txn->DeleteRuleByHandle(cTable, cForwardChain, r.mHandle);
        }
    }

    txn->FlushChain(cTable, chains.mInChain);
    txn->DeleteChain(cTable, chains.mInChain);
    txn->FlushChain(cTable, chains.mOutChain);
    txn->DeleteChain(cTable, chains.mOutChain);

    if (auto err = txn->Commit(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    {
        std::unique_lock lock {mMutex};

        for (const auto& chain : {chains.mInChain, chains.mOutChain}) {
            if (auto it = mTrafficData.find(chain); it != mTrafficData.end()) {
                if (auto err
                    = mStorage->SetTrafficMonitorData(chain.c_str(), it->second.mLastUpdate, it->second.mCurrentValue);
                    !err.IsNone()) {
                    LOG_ERR() << "Can't set traffic monitor data" << Log::Field("chain", chain.c_str())
                              << Log::Field(err);
                }

                mTrafficData.erase(it);
            }
        }

        mInstanceChains.erase(instanceID.CStr());
    }

    return ErrorEnum::eNone;
}

Error TrafficMonitor::GetSystemTraffic(uint64_t& inputTraffic, uint64_t& outputTraffic) const
{
    std::shared_lock lock {mMutex};

    LOG_DBG() << "Get system traffic data";

    return GetTrafficData(cInSystemChain, cOutSystemChain, inputTraffic, outputTraffic);
}

Error TrafficMonitor::GetInstanceTraffic(
    const String& instanceID, uint64_t& inputTraffic, uint64_t& outputTraffic) const
{
    std::shared_lock lock {mMutex};

    LOG_DBG() << "Get instance traffic data" << Log::Field("instanceID", instanceID);

    auto it = mInstanceChains.find(instanceID.CStr());
    if (it == mInstanceChains.end()) {
        return ErrorEnum::eNotFound;
    }

    return GetTrafficData(it->second.mInChain, it->second.mOutChain, inputTraffic, outputTraffic);
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

Error TrafficMonitor::CreateSystemChains()
{
    auto txn = mBackend->NewTxn();

    txn->AddTable(cTable);

    txn->AddBaseChain({cTable, cInputChain, nftables::FWChainTypeEnum::eFilter, nftables::FWHookEnum::eInput, 0,
        nftables::FWActionEnum::eAccept});
    txn->AddBaseChain({cTable, cOutputChain, nftables::FWChainTypeEnum::eFilter, nftables::FWHookEnum::eOutput, 0,
        nftables::FWActionEnum::eAccept});
    txn->AddBaseChain({cTable, cForwardChain, nftables::FWChainTypeEnum::eFilter, nftables::FWHookEnum::eForward, 0,
        nftables::FWActionEnum::eAccept});

    StagedTrafficData staged;

    if (auto err = CreateInstanceChain(*txn, cInSystemChain, true, "", cInputChain, 0, staged); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = CreateInstanceChain(*txn, cOutSystemChain, false, "", cOutputChain, 0, staged); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = txn->Commit(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    PublishTrafficData(staged);

    return ErrorEnum::eNone;
}

Error TrafficMonitor::DeleteTrafficTable()
{
    auto txn = mBackend->NewTxn();

    txn->DeleteTable(cTable);

    if (auto err = txn->Commit(); !err.IsNone() && !err.Is(ErrorEnum::eNotFound)) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error TrafficMonitor::CreateInstanceChain(nftables::FWTxnItf& txn, const std::string& chain, bool isInChain,
    const std::string& address, const std::string& parentBaseChain, uint64_t limit, StagedTrafficData& staged)
{
    LOG_DBG() << "Create traffic chain" << Log::Field("chain", chain.c_str());

    txn.AddChain({cTable, chain});

    if (auto err = AppendChainCounterRules(txn, chain, isInChain, address, false); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    nftables::FWRule jump {};

    if (!address.empty()) {
        (isInChain ? jump.mDstAddr : jump.mSrcAddr) = address;
    }

    jump.mAction     = nftables::FWActionEnum::eJump;
    jump.mJumpTarget = chain;

    if (auto err = txn.AddRule(cTable, parentBaseChain, jump); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    TrafficData traffic;

    traffic.mAddress = address;
    traffic.mLimit   = limit;

    if (auto err = mStorage->GetTrafficMonitorData(chain.c_str(), traffic.mLastUpdate, traffic.mInitialValue);
        !err.IsNone() && err != ErrorEnum::eNotFound) {
        return AOS_ERROR_WRAP(err);
    }

    staged.emplace_back(chain, std::move(traffic));

    return ErrorEnum::eNone;
}

void TrafficMonitor::PublishTrafficData(StagedTrafficData& staged)
{
    std::unique_lock lock {mMutex};

    for (auto& [chain, traffic] : staged) {
        mTrafficData[chain] = std::move(traffic);
    }
}

Error TrafficMonitor::AppendChainCounterRules(
    nftables::FWTxnItf& txn, const std::string& chain, bool isInChain, const std::string& address, bool disabled)
{
    if (disabled) {
        nftables::FWRule drop {};

        if (!address.empty()) {
            (isInChain ? drop.mDstAddr : drop.mSrcAddr) = address;
        }

        drop.mAction = nftables::FWActionEnum::eDrop;

        return txn.AddRule(cTable, chain, drop);
    }

    for (const auto* cidr : cSkipNetworks) {
        nftables::FWRule skip {};

        (isInChain ? skip.mSrcAddr : skip.mDstAddr) = cidr;
        skip.mAction                                = nftables::FWActionEnum::eReturn;

        if (auto err = txn.AddRule(cTable, chain, skip); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    nftables::FWRule counter {};

    if (!address.empty()) {
        (isInChain ? counter.mDstAddr : counter.mSrcAddr) = address;
    }

    // Count only, never decide policy: return to the caller chain after the
    // counter so the firewall table on the same hook stays authoritative.
    counter.mCounter = true;
    counter.mAction  = nftables::FWActionEnum::eReturn;

    return txn.AddRule(cTable, chain, counter);
}

Error TrafficMonitor::GetTrafficChainBytes(const std::string& chain, uint64_t& bytes)
{
    std::vector<nftables::FWListedRule> rules;

    if (auto err = mBackend->ListChainRules(cTable, chain, rules); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    const auto it
        = std::find_if(rules.cbegin(), rules.cend(), [](const nftables::FWListedRule& r) { return r.mRule.mCounter; });
    if (it == rules.cend()) {
        return ErrorEnum::eNotFound;
    }

    bytes = it->mBytes;

    return ErrorEnum::eNone;
}

Error TrafficMonitor::SetChainState(const std::string& chain, const std::string& address, bool enable)
{
    LOG_DBG() << "Set chain state" << Log::Field("chain", chain.c_str()) << Log::Field("enable", enable);

    const bool isInChain = HasPrefix(chain, cInChainPrefix);

    auto txn = mBackend->NewTxn();

    txn->FlushChain(cTable, chain);

    if (auto err = AppendChainCounterRules(*txn, chain, isInChain, address, !enable); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = txn->Commit(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error TrafficMonitor::UpdateTrafficData()
{
    std::unique_lock lock {mMutex};

    if (mStop) {
        return ErrorEnum::eNone;
    }

    LOG_DBG() << "Update traffic data";

    Error err = ErrorEnum::eNone;
    auto  now = Time::Now();

    for (auto& [chain, traffic] : mTrafficData) {
        uint64_t value {};

        if (!traffic.mDisabled) {
            if (auto chainErr = GetTrafficChainBytes(chain, value);
                !chainErr.IsNone() && !chainErr.Is(ErrorEnum::eNotFound)) {
                LOG_ERR() << "Can't get traffic chain bytes" << Log::Field("chain", chain.c_str())
                          << Log::Field(chainErr);

                if (err.IsNone()) {
                    err = chainErr;
                }
            }
        }

        if (!IsSamePeriod(mTrafficPeriod, now, traffic.mLastUpdate)) {
            LOG_DBG() << "Reset statistics" << Log::Field("chain", chain.c_str());

            traffic.mInitialValue = 0;
            traffic.mSubValue     = value;
        }

        traffic.mCurrentValue = traffic.mInitialValue + value - traffic.mSubValue;
        traffic.mLastUpdate   = now;

        LOG_DBG() << "Traffic data" << Log::Field("chain", chain.c_str()) << Log::Field("value", traffic.mCurrentValue);

        if (auto checkErr = CheckTrafficLimit(chain, traffic); !checkErr.IsNone()) {
            LOG_ERR() << "Can't check traffic limit" << Log::Field("chain", chain.c_str()) << Log::Field(checkErr);

            if (err.IsNone()) {
                err = checkErr;
            }
        }

        if (auto storageErr
            = mStorage->SetTrafficMonitorData(chain.c_str(), traffic.mLastUpdate, traffic.mCurrentValue);
            !storageErr.IsNone()) {
            LOG_ERR() << "Can't set traffic monitor data" << Log::Field("chain", chain.c_str())
                      << Log::Field(storageErr);

            if (err.IsNone()) {
                err = storageErr;
            }
        }
    }

    return err;
}

Error TrafficMonitor::CheckTrafficLimit(const std::string& chain, TrafficData& trafficData)
{
    if (trafficData.mLimit == 0) {
        return ErrorEnum::eNone;
    }

    if (trafficData.mCurrentValue > trafficData.mLimit && !trafficData.mDisabled) {
        if (auto err = SetChainState(chain, trafficData.mAddress, false); !err.IsNone()) {
            return err;
        }

        ResetTrafficData(trafficData, true);

        return ErrorEnum::eNone;
    }

    if (trafficData.mCurrentValue < trafficData.mLimit && trafficData.mDisabled) {
        if (auto err = SetChainState(chain, trafficData.mAddress, true); !err.IsNone()) {
            return err;
        }

        ResetTrafficData(trafficData, false);
    }

    return ErrorEnum::eNone;
}

void TrafficMonitor::ResetTrafficData(TrafficData& trafficData, bool disable)
{
    trafficData.mDisabled     = disable;
    trafficData.mInitialValue = trafficData.mCurrentValue;
    trafficData.mSubValue     = 0;
}

bool TrafficMonitor::IsSamePeriod(TrafficPeriodEnum trafficPeriod, const aos::Time& t1, const aos::Time& t2) const
{
    int y1 = 0, m1 = 0, d1 = 0, h1 = 0, min1 = 0;

    if (auto err = t1.GetDate(&d1, &m1, &y1); !err.IsNone()) {
        LOG_ERR() << "Can't get date" << Log::Field(err);

        return false;
    }

    if (auto err = t1.GetTime(&h1, &min1); !err.IsNone()) {
        LOG_ERR() << "Can't get time" << Log::Field(err);

        return false;
    }

    int y2 = 0, m2 = 0, d2 = 0, h2 = 0, min2 = 0;

    if (auto err = t2.GetDate(&d2, &m2, &y2); !err.IsNone()) {
        LOG_ERR() << "Can't get date" << Log::Field(err);

        return false;
    }

    if (auto err = t2.GetTime(&h2, &min2); !err.IsNone()) {
        LOG_ERR() << "Can't get time" << Log::Field(err);

        return false;
    }

    switch (trafficPeriod) {
    case TrafficPeriodEnum::eMinutePeriod:
        return y1 == y2 && m1 == m2 && d1 == d2 && h1 == h2 && min1 == min2;

    case TrafficPeriodEnum::eHourPeriod:
        return y1 == y2 && m1 == m2 && d1 == d2 && h1 == h2;

    case TrafficPeriodEnum::eDayPeriod:
        return y1 == y2 && m1 == m2 && d1 == d2;

    case TrafficPeriodEnum::eMonthPeriod:
        return y1 == y2 && m1 == m2;

    case TrafficPeriodEnum::eYearPeriod:
        return y1 == y2;

    default:
        return false;
    }
}

Error TrafficMonitor::GetTrafficData(
    const std::string& inChain, const std::string& outChain, uint64_t& inputTraffic, uint64_t& outputTraffic) const
{
    auto inIt  = mTrafficData.find(inChain);
    auto outIt = mTrafficData.find(outChain);

    if (inIt == mTrafficData.end() || outIt == mTrafficData.end()) {
        return ErrorEnum::eNotFound;
    }

    inputTraffic  = inIt->second.mCurrentValue;
    outputTraffic = outIt->second.mCurrentValue;

    return ErrorEnum::eNone;
}

} // namespace aos::sm::networkmanager
