/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <common/utils/exception.hpp>
#include <sm/logger/logmodule.hpp>

#include "trafficmonitor.hpp"

namespace aos::sm::networkmanager {

/***********************************************************************************************************************
 * Static functions
 **********************************************************************************************************************/

namespace {

bool HasSuffix(const std::string& chain, const std::string& suffix)
{
    return chain.length() >= suffix.length() && chain.substr(chain.length() - suffix.length()) == suffix;
}

bool HasPrefix(const std::string& chain, const std::string& prefix)
{
    return chain.length() >= prefix.length() && chain.compare(0, prefix.length(), prefix) == 0;
}

std::vector<std::string> SplitFields(const std::string& str)
{
    std::istringstream       iss(str);
    std::vector<std::string> tokens;
    std::string              token;

    while (iss >> token) {
        tokens.push_back(token);
    }

    return tokens;
}

} // namespace

/***********************************************************************************************************************
 * Public methods
 **********************************************************************************************************************/

Error TrafficMonitor::Init(StorageItf& storage, common::network::IPTablesItf& iptables, Duration updatePeriod)
{
    LOG_DBG() << "Init traffic monitor";

    mStorage       = &storage;
    mIPTables      = &iptables;
    mTrafficPeriod = TrafficPeriodEnum::eDayPeriod;
    mUpdatePeriod  = updatePeriod;

    if (auto err = DeleteAllTrafficChains(); !err.IsNone()) {
        return err;
    }

    if (auto err = CreateTrafficChain(cInSystemChain, "INPUT", "0/0", 0); !err.IsNone()) {
        return err;
    }

    if (auto err = CreateTrafficChain(cOutSystemChain, "OUTPUT", "0/0", 0); !err.IsNone()) {
        return err;
    }

    return ErrorEnum::eNone;
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
                LOG_ERR() << "Can't update traffic data: error=" << err;
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
        LOG_ERR() << "Can't stop timer: error=" << err;
    }

    return DeleteAllTrafficChains();
}

void TrafficMonitor::SetPeriod(TrafficPeriod period)
{
    std::unique_lock lock {mMutex};

    LOG_DBG() << "Set traffic period: period=" << static_cast<int>(period);

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

        LOG_DBG() << "Start instance monitoring: instanceID=" << instanceID.CStr();

        if (mInstanceChains.find(instanceID.CStr()) != mInstanceChains.end()) {
            return ErrorEnum::eNone;
        }
    }

    std::stringstream ss;

    ss << std::hex << std::hash<std::string> {}(instanceID.CStr());

    std::string chainBase = ss.str();

    InstanceChains serviceChains {
        "AOS_" + chainBase + "_IN",
        "AOS_" + chainBase + "_OUT",
    };

    if (auto err = CreateTrafficChain(serviceChains.mInChain, "FORWARD", IPAddress.CStr(), downloadLimit);
        err != ErrorEnum::eNone) {
        return err;
    }

    if (auto err = CreateTrafficChain(serviceChains.mOutChain, "FORWARD", IPAddress.CStr(), uploadLimit);
        err != ErrorEnum::eNone) {
        return err;
    }

    {
        std::unique_lock lock {mMutex};

        mInstanceChains[instanceID.CStr()] = std::move(serviceChains);
    }

    return ErrorEnum::eNone;
}

Error TrafficMonitor::StopInstanceMonitoring(const String& instanceID)
{
    if (instanceID.IsEmpty()) {
        return ErrorEnum::eNone;
    }

    decltype(mInstanceChains)::iterator it;

    {
        std::shared_lock lock {mMutex};

        LOG_DBG() << "Stop instance monitoring: instanceID=" << instanceID.CStr();

        if (it = mInstanceChains.find(instanceID.CStr()); it == mInstanceChains.end()) {
            return ErrorEnum::eNone;
        }
    }

    if (auto err = DeleteTrafficChain(it->second.mInChain, "FORWARD"); err != ErrorEnum::eNone) {
        LOG_ERR() << "Can't delete chain: error=" << err;
    }

    if (auto err = DeleteTrafficChain(it->second.mOutChain, "FORWARD"); err != ErrorEnum::eNone) {
        LOG_ERR() << "Can't delete chain: error=" << err;
    }

    {
        std::unique_lock lock {mMutex};

        mInstanceChains.erase(it);
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

    LOG_DBG() << "Get instance traffic data: instanceID=" << instanceID.CStr();

    auto it = mInstanceChains.find(instanceID.CStr());
    if (it == mInstanceChains.end()) {
        return ErrorEnum::eNotFound;
    }

    return GetTrafficData(it->second.mInChain, it->second.mOutChain, inputTraffic, outputTraffic);
}

/***********************************************************************************************************************
 * Private methods
 **********************************************************************************************************************/

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
                LOG_ERR() << "Can't get traffic chain bytes: chain=" << chain.c_str() << ", error=" << chainErr;

                if (err.IsNone()) {
                    err = chainErr;
                }
            }
        }

        if (!IsSamePeriod(mTrafficPeriod, now, traffic.mLastUpdate)) {
            LOG_DBG() << "Reset statistics: chain=" << chain.c_str();

            traffic.mInitialValue = 0;
            traffic.mSubValue     = value;
        }

        LOG_DBG() << "Update traffic data: chain=" << chain.c_str() << ", value=" << value;

        // initialValue is used to keep traffic between resets
        traffic.mCurrentValue = traffic.mInitialValue + value - traffic.mSubValue;
        traffic.mLastUpdate   = now;

        LOG_DBG() << "Traffic data: chain=" << chain.c_str() << ", value=" << traffic.mCurrentValue;

        if (auto checkErr = CheckTrafficLimit(chain, traffic); !checkErr.IsNone()) {
            LOG_ERR() << "Can't check traffic limit: chain=" << chain.c_str() << ", error=" << checkErr;

            if (err.IsNone()) {
                err = checkErr;
            }
        }

        if (auto storageErr
            = mStorage->SetTrafficMonitorData(chain.c_str(), traffic.mLastUpdate, traffic.mCurrentValue);
            !storageErr.IsNone()) {
            LOG_ERR() << "Can't set traffic monitor data: chain=" << chain.c_str() << ", error=" << storageErr;

            if (err.IsNone()) {
                err = storageErr;
            }
        }
    }

    return ErrorEnum::eNone;
}

Error TrafficMonitor::CheckTrafficLimit(const std::string& chain, TrafficData& trafficData)
{
    if (trafficData.mLimit == 0) {
        return ErrorEnum::eNone;
    }

    if (trafficData.mCurrentValue > trafficData.mLimit && !trafficData.mDisabled) {
        // disable chain
        if (auto err = SetChainState(chain, trafficData.mAddresses, false); !err.IsNone()) {
            return err;
        }

        ResetTrafficData(trafficData, true);

        return ErrorEnum::eNone;
    }

    if (trafficData.mCurrentValue < trafficData.mLimit && trafficData.mDisabled) {
        // enable chain
        if (auto err = SetChainState(chain, trafficData.mAddresses, true); !err.IsNone()) {
            return err;
        }

        ResetTrafficData(trafficData, false);
    }

    return ErrorEnum::eNone;
}

Error TrafficMonitor::SetChainState(const std::string& chain, const std::string& addresses, bool enable)
{
    LOG_DBG() << "Set chain state: chain=" << chain.c_str() << ", state=" << enable;

    const bool isInChain  = HasSuffix(chain, "_IN");
    const bool isOutChain = HasSuffix(chain, "_OUT");

    if (enable) {
        if (auto err = DeleteChainRule(chain,
                mIPTables->CreateRule()
                    .Destination(isInChain ? addresses : "")
                    .Source(isOutChain ? addresses : "")
                    .Jump("DROP"));
            err != ErrorEnum::eNone) {
            return AOS_ERROR_WRAP(err);
        }

        if (auto err = mIPTables->Append(chain,
                mIPTables->CreateRule().Destination(isInChain ? addresses : "").Source(isOutChain ? addresses : ""));
            err != ErrorEnum::eNone) {
            return AOS_ERROR_WRAP(err);
        }

        return ErrorEnum::eNone;
    }

    if (auto err = DeleteChainRule(
            chain, mIPTables->CreateRule().Destination(isInChain ? addresses : "").Source(isOutChain ? addresses : ""));
        err != ErrorEnum::eNone) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = mIPTables->Append(chain,
            mIPTables->CreateRule()
                .Destination(isInChain ? addresses : "")
                .Source(isOutChain ? addresses : "")
                .Jump("DROP"));
        err != ErrorEnum::eNone) {
        return AOS_ERROR_WRAP(err);
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
        LOG_ERR() << "Can't get date: error=" << err;

        return false;
    }

    if (auto err = t1.GetTime(&h1, &min1); !err.IsNone()) {
        LOG_ERR() << "Can't get time: error=" << err;

        return false;
    }

    int y2 = 0, m2 = 0, d2 = 0, h2 = 0, min2 = 0;

    if (auto err = t2.GetDate(&d2, &m2, &y2); !err.IsNone()) {
        LOG_ERR() << "Can't get date: error=" << err;

        return false;
    }

    if (auto err = t2.GetTime(&h2, &min2); !err.IsNone()) {
        LOG_ERR() << "Can't get time: error=" << err;

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

Error TrafficMonitor::GetTrafficChainBytes(const std::string& chain, uint64_t& bytes)
{
    auto [rules, err] = mIPTables->ListAllRulesWithCounters(chain);
    if (err != ErrorEnum::eNone) {
        return AOS_ERROR_WRAP(err);
    }

    auto items = SplitFields(rules.back());

    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i] == "-c" && i + 2 < items.size()) {
            try {
                bytes = std::stoull(items[i + 2]);

                return ErrorEnum::eNone;
            } catch (const std::exception& e) {
                return AOS_ERROR_WRAP(common::utils::ToAosError(e, ErrorEnum::eInvalidArgument));
            }
        }
    }

    return ErrorEnum::eNotFound;
}

Error TrafficMonitor::DeleteAllTrafficChains()
{
    auto [chains, err] = mIPTables->ListChains();
    if (!err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    for (const auto& chain : chains) {
        if (!HasPrefix(chain, "AOS_")) {
            continue;
        }

        if (chain == cInSystemChain) {
            err = DeleteTrafficChain(chain, "INPUT");
        } else if (chain == cOutSystemChain) {
            err = DeleteTrafficChain(chain, "OUTPUT");
        } else if (HasSuffix(chain, "_IN")) {
            err = DeleteTrafficChain(chain, "FORWARD");
        } else if (HasSuffix(chain, "_OUT")) {
            err = DeleteTrafficChain(chain, "FORWARD");
        }

        if (!err.IsNone()) {
            LOG_ERR() << "Can't delete: chain=" << chain.c_str() << ", error=" << err;
        }
    }

    return ErrorEnum::eNone;
}

Error TrafficMonitor::DeleteTrafficChain(const std::string& chain, const std::string& rootChain)
{
    if (chain.empty()) {
        return ErrorEnum::eNone;
    }

    {
        std::unique_lock lock {mMutex};

        LOG_INF() << "Delete chain: " << chain.c_str();

        if (auto it = mTrafficData.find(chain); it != mTrafficData.end()) {
            if (auto err
                = mStorage->SetTrafficMonitorData(chain.c_str(), it->second.mLastUpdate, it->second.mCurrentValue);
                !err.IsNone()) {
                LOG_ERR() << "Can't set traffic monitor data: chain=" << chain.c_str() << ", error=" << err;
            }

            mTrafficData.erase(it);
        }
    }

    if (auto err = DeleteChainRule(rootChain, mIPTables->CreateRule().Jump(chain)); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = mIPTables->ClearChain(chain); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = mIPTables->DeleteChain(chain); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error TrafficMonitor::DeleteChainRule(const std::string& chain, const common::network::RuleBuilder& builder)
{
    return mIPTables->DeleteRule(chain, builder);
}

Error TrafficMonitor::CreateTrafficChain(
    const std::string& chain, const std::string& rootChain, const std::string& addresses, uint64_t limit)
{
    LOG_DBG() << "Create chain: " << chain.c_str();

    if (auto err = mIPTables->NewChain(chain); err != ErrorEnum::eNone) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = mIPTables->Insert(rootChain, 1, mIPTables->CreateRule().Jump(chain)); err != ErrorEnum::eNone) {
        return AOS_ERROR_WRAP(err);
    }

    const bool isInChain  = HasSuffix(chain, "_IN");
    const bool isOutChain = HasSuffix(chain, "_OUT");

    if (isInChain || isOutChain) {
        if (auto err = mIPTables->Append(chain,
                mIPTables->CreateRule()
                    .Source(isInChain ? cSkipNetworks : "")
                    .Destination(isOutChain ? cSkipNetworks : "")
                    .Jump("RETURN"));
            err != ErrorEnum::eNone) {
            return AOS_ERROR_WRAP(err);
        }

        if (auto err = mIPTables->Append(chain,
                mIPTables->CreateRule().Source(isOutChain ? addresses : "").Destination(isInChain ? addresses : ""));
            err != ErrorEnum::eNone) {
            return AOS_ERROR_WRAP(err);
        }
    }

    TrafficData traffic;

    if (limit != 0) {
        traffic.mLimit = limit;
    }

    LOG_DBG() << "Initial traffic data: chain=" << chain.c_str() << ", limit=" << traffic.mLimit;

    if (auto err = mStorage->GetTrafficMonitorData(chain.c_str(), traffic.mLastUpdate, traffic.mInitialValue);
        err != ErrorEnum::eNone && err != ErrorEnum::eNotFound) {
        return AOS_ERROR_WRAP(err);
    }

    {
        std::unique_lock lock {mMutex};

        mTrafficData[chain] = std::move(traffic);
    }

    return ErrorEnum::eNone;
}

} // namespace aos::sm::networkmanager
