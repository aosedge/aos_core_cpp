/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <string>

#include <core/common/tools/logger.hpp>

#include "firewall.hpp"

namespace aos::sm::networkmanager {

namespace {

RetWithError<uint16_t> ParsePort(const String& port)
{
    if (port.IsEmpty()) {
        return {uint16_t {0}, ErrorEnum::eNone};
    }

    char*      end = nullptr;
    const auto val = std::strtoul(port.CStr(), &end, 10);

    if (end == port.CStr() || *end != '\0' || val == 0 || val > std::numeric_limits<uint16_t>::max()) {
        return {uint16_t {0}, AOS_ERROR_WRAP(Error(ErrorEnum::eInvalidArgument, "invalid port"))};
    }

    return {static_cast<uint16_t>(val), ErrorEnum::eNone};
}

std::string ChainName(const String& instanceID)
{
    std::string name {"instance_"};

    name.reserve(name.size() + instanceID.Size());

    for (size_t i = 0; i < instanceID.Size(); ++i) {
        const auto c = instanceID[i];

        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
            name += c;
        } else {
            name += '_';
        }
    }

    return name;
}

Error AppendInstanceRules(common::network::FWTxnItf& txn, const std::string& table, const std::string& chain,
    const InstanceFirewallParams& params)
{
    const std::string instanceIP {params.mIP.CStr()};

    for (const auto& in : params.mInput) {
        uint16_t port = 0;
        Error    err;

        Tie(port, err) = ParsePort(in.mPort);
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        common::network::FWRule r {};

        r.mDstAddr = instanceIP;
        r.mProto   = in.mProtocol.CStr();
        r.mDstPort = port;
        r.mAction  = common::network::FWActionEnum::eAccept;

        if (auto err = txn.AddRule(table, chain, r); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    for (const auto& out : params.mOutput) {
        uint16_t port = 0;
        Error    err;

        Tie(port, err) = ParsePort(out.mDstPort);
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (!out.mSrcIP.IsEmpty() && std::string {out.mSrcIP.CStr()} != instanceIP) {
            LOG_WRN() << "Output rule mSrcIP overridden by instance IP" << Log::Field("srcIP", out.mSrcIP)
                      << Log::Field("instanceIP", params.mIP);
        }

        common::network::FWRule r {};

        r.mSrcAddr = instanceIP;
        r.mDstAddr = out.mDstIP.CStr();
        r.mProto   = out.mProto.CStr();
        r.mDstPort = port;
        r.mAction  = common::network::FWActionEnum::eAccept;

        if (auto err = txn.AddRule(table, chain, r); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    common::network::FWRule terminalIn {};
    terminalIn.mDstAddr = instanceIP;
    terminalIn.mAction  = common::network::FWActionEnum::eDrop;

    if (auto err = txn.AddRule(table, chain, terminalIn); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    common::network::FWRule terminalOut {};
    terminalOut.mSrcAddr = instanceIP;
    terminalOut.mAction
        = params.mAllowPublic ? common::network::FWActionEnum::eAccept : common::network::FWActionEnum::eDrop;

    if (auto err = txn.AddRule(table, chain, terminalOut); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error Firewall::Init(common::network::FWBackendItf& backend)
{
    mBackend = &backend;

    return ErrorEnum::eNone;
}

Error Firewall::Start()
{
    LOG_DBG() << "Start firewall";

    auto txn = mBackend->NewTxn();

    txn->DeleteTable(mTable);

    if (auto err = txn->Commit(); !err.IsNone()) {
        LOG_DBG() << "Stale table cleanup skipped" << Log::Field(err);
    }

    txn->AddTable(mTable);
    txn->AddBaseChain({mTable, cForwardChain, common::network::FWChainTypeEnum::eFilter,
        common::network::FWHookEnum::eForward, cForwardPriority});
    txn->AddBaseChain({mTable, cPostroutingChain, common::network::FWChainTypeEnum::eNAT,
        common::network::FWHookEnum::ePostrouting, cNATPriority});

    // Connection tracking gates the per-instance access rules: drop garbage
    // early and let reply traffic of allowed flows back in, so the access
    // rules only need to describe connection initiation.
    common::network::FWRule ctInvalid {};
    ctInvalid.mCtState = "invalid";
    ctInvalid.mAction  = common::network::FWActionEnum::eDrop;

    if (auto err = txn->AddRule(mTable, cForwardChain, ctInvalid); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    common::network::FWRule ctEstablished {};
    ctEstablished.mCtState = "established,related";
    ctEstablished.mAction  = common::network::FWActionEnum::eAccept;

    if (auto err = txn->AddRule(mTable, cForwardChain, ctEstablished); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = txn->Commit(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    mMasqueradeRules.clear();

    return ErrorEnum::eNone;
}

Error Firewall::Stop()
{
    LOG_DBG() << "Stop firewall";

    auto txn = mBackend->NewTxn();

    txn->DeleteTable(mTable);

    if (auto err = txn->Commit(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    mMasqueradeRules.clear();

    return ErrorEnum::eNone;
}

Error Firewall::AddInstance(const String& instanceID, const InstanceFirewallParams& params)
{
    LOG_DBG() << "Add firewall instance" << Log::Field("instanceID", instanceID);

    const auto chain = ChainName(instanceID);

    auto txn = mBackend->NewTxn();

    txn->AddChain({mTable, chain});

    if (auto err = AppendInstanceRules(*txn, mTable, chain, params); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    common::network::FWRule jumpIn {};
    jumpIn.mDstAddr    = params.mIP.CStr();
    jumpIn.mAction     = common::network::FWActionEnum::eJump;
    jumpIn.mJumpTarget = chain;

    if (auto err = txn->AddRule(mTable, cForwardChain, jumpIn); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    common::network::FWRule jumpOut {};
    jumpOut.mSrcAddr    = params.mIP.CStr();
    jumpOut.mAction     = common::network::FWActionEnum::eJump;
    jumpOut.mJumpTarget = chain;

    if (auto err = txn->AddRule(mTable, cForwardChain, jumpOut); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = txn->Commit(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error Firewall::RemoveInstance(const String& instanceID)
{
    LOG_DBG() << "Remove firewall instance" << Log::Field("instanceID", instanceID);

    const auto chain = ChainName(instanceID);

    std::vector<common::network::FWListedRule> forwardRules;

    if (auto err = mBackend->ListChainRules(mTable, cForwardChain, forwardRules); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    std::vector<common::network::FWRuleHandle> jumpHandles;

    for (const auto& r : forwardRules) {
        if (r.mRule.mAction == common::network::FWActionEnum::eJump && r.mRule.mJumpTarget == chain) {
            jumpHandles.push_back(r.mHandle);
        }
    }

    if (jumpHandles.empty()) {
        return ErrorEnum::eNone;
    }

    auto txn = mBackend->NewTxn();

    for (const auto handle : jumpHandles) {
        txn->DeleteRuleByHandle(mTable, cForwardChain, handle);
    }

    txn->FlushChain(mTable, chain);
    txn->DeleteChain(mTable, chain);

    if (auto err = txn->Commit(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error Firewall::UpdateInstance(const String& instanceID, const InstanceFirewallParams& params)
{
    LOG_DBG() << "Update firewall instance" << Log::Field("instanceID", instanceID);

    const auto chain = ChainName(instanceID);

    auto txn = mBackend->NewTxn();

    txn->FlushChain(mTable, chain);

    if (auto err = AppendInstanceRules(*txn, mTable, chain, params); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = txn->Commit(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error Firewall::AddMasquerade(const String& subnet, const String& outIf)
{
    LOG_DBG() << "Add masquerade" << Log::Field("subnet", subnet) << Log::Field("outIf", outIf);

    std::pair<std::string, std::string> key {subnet.CStr(), outIf.CStr()};

    if (mMasqueradeRules.count(key) != 0) {
        return ErrorEnum::eNone;
    }

    common::network::FWRule r {};
    r.mSrcAddr = key.first;
    r.mOIFName = key.second;
    r.mAction  = common::network::FWActionEnum::eMasquerade;

    auto txn = mBackend->NewTxn();

    if (auto err = txn->AddRule(mTable, cPostroutingChain, r); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (auto err = txn->Commit(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    mMasqueradeRules.insert(std::move(key));

    return ErrorEnum::eNone;
}

Error Firewall::RemoveMasquerade(const String& subnet, const String& outIf)
{
    LOG_DBG() << "Remove masquerade" << Log::Field("subnet", subnet) << Log::Field("outIf", outIf);

    std::pair<std::string, std::string> key {subnet.CStr(), outIf.CStr()};

    std::vector<common::network::FWListedRule> postRules;

    if (auto err = mBackend->ListChainRules(mTable, cPostroutingChain, postRules); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    const auto it = std::find_if(postRules.begin(), postRules.end(), [&key](const common::network::FWListedRule& r) {
        return r.mRule.mAction == common::network::FWActionEnum::eMasquerade && r.mRule.mSrcAddr == key.first
            && r.mRule.mOIFName == key.second;
    });

    if (it == postRules.end()) {
        mMasqueradeRules.erase(key);

        return ErrorEnum::eNone;
    }

    auto txn = mBackend->NewTxn();

    txn->DeleteRuleByHandle(mTable, cPostroutingChain, it->mHandle);

    if (auto err = txn->Commit(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    mMasqueradeRules.erase(key);

    return ErrorEnum::eNone;
}

} // namespace aos::sm::networkmanager
