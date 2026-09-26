/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <algorithm>
#include <cctype>
#include <string>

#include <core/common/tools/logger.hpp>

#include <common/network/netpools.hpp>
#include <common/utils/parser.hpp>

#include "firewall.hpp"

namespace aos::sm::networkmanager {

namespace {

RetWithError<common::utils::PortRange> ParsePortRange(const String& port)
{
    if (port.IsEmpty()) {
        return {common::utils::PortRange {}, ErrorEnum::eNone};
    }

    const auto range = common::utils::ParsePortRange(port.CStr());

    if (!range.has_value()) {
        return {common::utils::PortRange {}, AOS_ERROR_WRAP(Error(ErrorEnum::eInvalidArgument, "invalid port"))};
    }

    return {*range, ErrorEnum::eNone};
}

void SetDstPort(nftables::FWRule& rule, const common::utils::PortRange& range)
{
    rule.mDstPort = range.mFirst;

    if (range.mLast > range.mFirst) {
        rule.mDstPortEnd = range.mLast;
    }
}

Error CheckPortProto(const common::utils::PortRange& range, const String& proto)
{
    if (range.mFirst == 0) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eInvalidArgument, "access rule requires a port"));
    }

    const std::string value {proto.CStr()};

    if (!value.empty() && value != "tcp" && value != "udp") {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eInvalidArgument, "unsupported protocol"));
    }

    return ErrorEnum::eNone;
}

std::string ProtoOrDefault(const String& proto, const common::utils::PortRange& range)
{
    // A port match needs a transport protocol; default to tcp (matching the
    // historical CNI/networkmanager convention). Without it an empty proto with
    // a port would widen the rule to all traffic to/from the instance.

    if (range.mFirst != 0 && proto.IsEmpty()) {
        return "tcp";
    }

    return proto.CStr();
}

Error AppendInstanceRules(nftables::FWTxnItf& txn, const std::string& table, const std::string& chain,
    const InstanceFirewallParams& params, bool output)
{
    const std::string instanceIP {params.mIP.CStr()};

    // Same-network communication is unrestricted in both directions.

    if (!params.mSubnet.IsEmpty()) {
        nftables::FWRule sameNetwork {};

        sameNetwork.mSrcAddr = output ? instanceIP : params.mSubnet.CStr();
        sameNetwork.mDstAddr = output ? params.mSubnet.CStr() : instanceIP;
        sameNetwork.mAction  = nftables::FWActionEnum::eReturn;

        if (auto err = txn.AddRule(table, chain, sameNetwork); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    if (!output) {
        for (const auto& in : params.mInput) {
            common::utils::PortRange range {};
            Error                    err;

            Tie(range, err) = ParsePortRange(in.mPort);

            if (!err.IsNone()) {
                return AOS_ERROR_WRAP(err);
            }

            // An input entry requires a port and only tcp/udp are supported (an
            // empty protocol defaults to tcp). Matches the aos_cni_firewall plugin.

            if (err = CheckPortProto(range, in.mProtocol); !err.IsNone()) {
                return AOS_ERROR_WRAP(err);
            }

            nftables::FWRule r {};

            r.mDstAddr = instanceIP;
            r.mProto   = ProtoOrDefault(in.mProtocol, range);
            r.mAction  = nftables::FWActionEnum::eReturn;

            SetDstPort(r, range);

            if (err = txn.AddRule(table, chain, r); !err.IsNone()) {
                return AOS_ERROR_WRAP(err);
            }
        }

    } else {
        for (const auto& out : params.mOutput) {
            common::utils::PortRange range {};
            Error                    err;

            Tie(range, err) = ParsePortRange(out.mDstPort);

            if (!err.IsNone()) {
                return AOS_ERROR_WRAP(err);
            }

            // An output entry must name a destination; an empty one collapses to a
            // bare source match that bypasses destination checks and
            // the AllowPublic terminal verdict. It is validated as strictly as an
            // input entry: a destination IP and port, with tcp/udp (empty -> tcp).

            if (out.mDstIP.IsEmpty()) {
                return AOS_ERROR_WRAP(Error(ErrorEnum::eInvalidArgument, "output access requires a destination IP"));
            }

            if (err = CheckPortProto(range, out.mProto); !err.IsNone()) {
                return AOS_ERROR_WRAP(err);
            }

            if (!out.mSrcIP.IsEmpty() && std::string {out.mSrcIP.CStr()} != instanceIP) {
                LOG_WRN() << "Output rule mSrcIP overridden by instance IP" << Log::Field("srcIP", out.mSrcIP)
                          << Log::Field("instanceIP", params.mIP);
            }

            nftables::FWRule r {};

            r.mSrcAddr = instanceIP;
            r.mDstAddr = out.mDstIP.CStr();
            r.mProto   = ProtoOrDefault(out.mProto, range);
            r.mAction  = nftables::FWActionEnum::eReturn;

            SetDstPort(r, range);

            if (err = txn.AddRule(table, chain, r); !err.IsNone()) {
                return AOS_ERROR_WRAP(err);
            }
        }

        // Public access never authorizes another AoS network, including remote nodes.

        for (const auto& pool : common::network::cNetworkPools) {
            nftables::FWRule denyNetwork {};

            denyNetwork.mDstAddr = pool.mSubnet;
            denyNetwork.mAction  = nftables::FWActionEnum::eDrop;

            if (auto err = txn.AddRule(table, chain, denyNetwork); !err.IsNone()) {
                return AOS_ERROR_WRAP(err);
            }
        }
    }

    nftables::FWRule terminal {};

    terminal.mAction = output && params.mAllowPublic ? nftables::FWActionEnum::eReturn : nftables::FWActionEnum::eDrop;

    return txn.AddRule(table, chain, terminal);
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error Firewall::Init(nftables::FWBackendItf& backend)
{
    mBackend = &backend;

    return ErrorEnum::eNone;
}

std::string Firewall::ChainName(const String& instanceID)
{
    std::string name {cInstanceChainPrefix};

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

Error Firewall::Start()
{
    LOG_DBG() << "Start firewall";

    std::vector<nftables::FWListedRule> forwardRules;

    // The table is provisioned ahead of SM and outlives it, so a listable
    // forward chain means it already exists: adopt it as is. Rules left by a
    // crashed SM still protect the instances that kept running, so they are
    // reaped later by RemoveOrphans rather than dropped here. If the table is
    // absent, build a fail-closed skeleton ourselves.

    if (!mBackend->ListChainRules(mTable, cForwardChain, forwardRules).IsNone()) {
        if (auto err = CreateSkeleton(); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    {
        std::lock_guard lock {mBatchMutex};

        mInstanceJumps.clear();
    }

    mMasqueradeRules.clear();

    return ErrorEnum::eNone;
}

Error Firewall::RemoveOrphans(
    const Array<StaticString<cIDLen>>& knownInstanceIDs, const Array<MasqueradeParams>& knownMasquerades)
{
    LOG_DBG() << "Remove orphan firewall artifacts";

    std::vector<nftables::FWListedRule> forwardRules;

    if (auto err = mBackend->ListChainRules(mTable, cIngressChain, forwardRules); !err.IsNone()) {
        return ErrorEnum::eNone;
    }

    std::set<std::string> knownChains;

    for (const auto& instanceID : knownInstanceIDs) {
        knownChains.emplace(ChainName(instanceID));
    }

    std::set<std::string> orphanChains;

    for (const auto& r : forwardRules) {
        if (r.mRule.mAction != nftables::FWActionEnum::eJump || r.mRule.mJumpTarget.rfind(cInstanceChainPrefix, 0) != 0
            || knownChains.count(r.mRule.mJumpTarget) != 0) {
            continue;
        }

        orphanChains.insert(r.mRule.mJumpTarget);
    }

    std::vector<nftables::FWListedRule> postRules;

    if (auto err = mBackend->ListChainRules(mTable, cPostroutingChain, postRules); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    std::set<std::pair<std::string, std::string>> known;

    for (const auto& masquerade : knownMasquerades) {
        known.emplace(masquerade.mSubnet.CStr(), masquerade.mOutIfName.CStr());
    }

    std::vector<nftables::FWRuleHandle> masqueradeHandles;

    mMasqueradeRules.clear();

    for (const auto& r : postRules) {
        if (r.mRule.mAction != nftables::FWActionEnum::eMasquerade) {
            continue;
        }

        std::pair<std::string, std::string> key {r.mRule.mSrcAddr, r.mRule.mOIFName};

        if (!r.mRule.mOIFNeg && known.count(key) != 0 && mMasqueradeRules.insert(key).second) {
            continue;
        }

        masqueradeHandles.push_back(r.mHandle);
    }

    if (orphanChains.empty() && masqueradeHandles.empty()) {
        return ErrorEnum::eNone;
    }

    auto txn = mBackend->NewTxn();

    for (const auto& chain : orphanChains) {
        std::vector<nftables::FWRuleHandle> handles;

        if (auto err = FindInstanceRules(chain, handles); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (!handles.empty()) {
            DeleteInstanceChain(*txn, chain, handles);
        }
    }

    for (const auto handle : masqueradeHandles) {
        txn->DeleteRuleByHandle(mTable, cPostroutingChain, handle);
    }

    return txn->Commit();
}

Error Firewall::Stop()
{
    LOG_DBG() << "Stop firewall";

    std::vector<nftables::FWListedRule> forwardRules;

    // Keep the table and base chains (they outlive SM); drop only the
    // per-instance state we added. Nothing to do if the table is already gone.

    if (auto err = mBackend->ListChainRules(mTable, cIngressChain, forwardRules); !err.IsNone()) {
        {
            std::lock_guard lock {mBatchMutex};

            mInstanceJumps.clear();
        }

        mMasqueradeRules.clear();

        return ErrorEnum::eNone;
    }

    if (auto err = ReconcileArtifacts(forwardRules); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    {
        std::lock_guard lock {mBatchMutex};

        mInstanceJumps.clear();
    }

    mMasqueradeRules.clear();

    return ErrorEnum::eNone;
}

Error Firewall::CreateSkeleton()
{
    auto  transaction = mBackend->NewTxn();
    auto& txn         = *transaction;

    txn.AddTable(mTable);
    txn.AddBaseChain({mTable, cForwardChain, nftables::FWChainTypeEnum::eFilter, nftables::FWHookEnum::eForward,
        cForwardPriority, nftables::FWActionEnum::eDrop});
    txn.AddBaseChain({mTable, cPostroutingChain, nftables::FWChainTypeEnum::eNAT, nftables::FWHookEnum::ePostrouting,
        cNATPriority, nftables::FWActionEnum::eAccept});

    for (const auto* chain : {cEgressChain, cIngressChain, cAcceptedChain}) {
        txn.AddChain({mTable, chain});
    }

    nftables::FWRule invalid {};

    invalid.mCtState = "invalid";
    invalid.mAction  = nftables::FWActionEnum::eDrop;

    if (auto err = txn.AddRule(mTable, cForwardChain, invalid); !err.IsNone()) {
        return err;
    }

    nftables::FWRule established {};

    established.mCtState = "established,related";
    established.mAction  = nftables::FWActionEnum::eAccept;

    if (auto err = txn.AddRule(mTable, cForwardChain, established); !err.IsNone()) {
        return err;
    }

    for (const auto* chain : {cEgressChain, cIngressChain, cAcceptedChain}) {
        nftables::FWRule jump {};

        jump.mAction     = nftables::FWActionEnum::eJump;
        jump.mJumpTarget = chain;

        if (auto err = txn.AddRule(mTable, cForwardChain, jump); !err.IsNone()) {
            return err;
        }
    }

    return txn.Commit();
}

Error Firewall::ReconcileArtifacts(const std::vector<nftables::FWListedRule>& forwardRules)
{
    // Ingress jumps identify the instance chains to remove.
    std::set<std::string> instanceChains;

    for (const auto& r : forwardRules) {
        if (r.mRule.mAction == nftables::FWActionEnum::eJump
            && r.mRule.mJumpTarget.rfind(cInstanceChainPrefix, 0) == 0) {
            instanceChains.insert(r.mRule.mJumpTarget);
        }
    }

    std::vector<nftables::FWListedRule> postRules;

    if (auto err = mBackend->ListChainRules(mTable, cPostroutingChain, postRules); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    std::vector<nftables::FWRuleHandle> masqueradeHandles;

    for (const auto& r : postRules) {
        if (r.mRule.mAction == nftables::FWActionEnum::eMasquerade) {
            masqueradeHandles.push_back(r.mHandle);
        }
    }

    if (instanceChains.empty() && masqueradeHandles.empty()) {
        return ErrorEnum::eNone;
    }

    auto txn = mBackend->NewTxn();

    for (const auto& chain : instanceChains) {
        std::vector<nftables::FWRuleHandle> handles;

        if (auto err = FindInstanceRules(chain, handles); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (!handles.empty()) {
            DeleteInstanceChain(*txn, chain, handles);
        }
    }

    for (const auto handle : masqueradeHandles) {
        txn->DeleteRuleByHandle(mTable, cPostroutingChain, handle);
    }

    return txn->Commit();
}

Error Firewall::AddInstance(const String& instanceID, const InstanceFirewallParams& params)
{
    LOG_DBG() << "Add firewall instance" << Log::Field("instanceID", instanceID);

    // Without an instance IP the parent jumps lose their address match and
    // become global FORWARD jumps, and the terminal rules match everything.

    if (params.mIP.IsEmpty()) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eInvalidArgument, "instance IP required"));
    }

    const auto chain = ChainName(instanceID);

    {
        std::lock_guard lock {mBatchMutex};

        if (mBatchMode && mBatchTxn) {
            if (auto err = AppendInstanceChain(*mBatchTxn, chain, params); !err.IsNone()) {
                return AOS_ERROR_WRAP(err);
            }

            mBatchChains.insert(chain);

            return ErrorEnum::eNone;
        }
    }

    auto txn = mBackend->NewTxn();

    if (auto err = AppendInstanceChain(*txn, chain, params); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    std::vector<nftables::FWRuleHandle> handles;

    if (auto err = txn->Commit(handles); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (handles.size() >= cNumInstanceHandles) {
        std::lock_guard lock {mBatchMutex};

        mInstanceJumps[chain] = {handles.end() - cNumInstanceHandles, handles.end()};
    }

    return ErrorEnum::eNone;
}

Error Firewall::AppendInstanceChain(
    nftables::FWTxnItf& txn, const std::string& chain, const InstanceFirewallParams& params)
{
    const auto outputChain = chain + "_out";

    txn.AddChain({mTable, chain});
    txn.AddChain({mTable, outputChain});

    if (auto err = AppendInstanceRules(txn, mTable, chain, params, false); !err.IsNone()) {
        return err;
    }

    if (auto err = AppendInstanceRules(txn, mTable, outputChain, params, true); !err.IsNone()) {
        return err;
    }

    nftables::FWRule incoming {};

    incoming.mDstAddr    = params.mIP.CStr();
    incoming.mAction     = nftables::FWActionEnum::eJump;
    incoming.mJumpTarget = chain;

    if (auto err = txn.AddRule(mTable, cIngressChain, incoming); !err.IsNone()) {
        return err;
    }

    nftables::FWRule outgoing {};

    outgoing.mSrcAddr    = params.mIP.CStr();
    outgoing.mAction     = nftables::FWActionEnum::eJump;
    outgoing.mJumpTarget = outputChain;

    if (auto err = txn.AddRule(mTable, cEgressChain, outgoing); !err.IsNone()) {
        return err;
    }

    // Only traffic involving a registered local instance can pass the base policy.
    incoming.mAction = nftables::FWActionEnum::eAccept;
    incoming.mJumpTarget.clear();
    outgoing.mAction = nftables::FWActionEnum::eAccept;
    outgoing.mJumpTarget.clear();

    if (auto err = txn.AddRule(mTable, cAcceptedChain, incoming); !err.IsNone()) {
        return err;
    }

    return txn.AddRule(mTable, cAcceptedChain, outgoing);
}

Error Firewall::FindInstanceRules(const std::string& chain, std::vector<nftables::FWRuleHandle>& handles)
{
    std::vector<nftables::FWListedRule> ingress;

    if (auto err = mBackend->ListChainRules(mTable, cIngressChain, ingress); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    const auto incoming = std::find_if(ingress.begin(), ingress.end(), [&chain](const auto& entry) {
        return entry.mRule.mAction == nftables::FWActionEnum::eJump && entry.mRule.mJumpTarget == chain;
    });

    if (incoming == ingress.end()) {
        return ErrorEnum::eNone;
    }

    handles.push_back(incoming->mHandle);

    std::vector<nftables::FWListedRule> egress;

    if (auto err = mBackend->ListChainRules(mTable, cEgressChain, egress); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    for (const auto& entry : egress) {
        if (entry.mRule.mAction == nftables::FWActionEnum::eJump && entry.mRule.mJumpTarget == chain + "_out") {
            handles.push_back(entry.mHandle);
            break;
        }
    }

    std::vector<nftables::FWListedRule> accepted;

    if (auto err = mBackend->ListChainRules(mTable, cAcceptedChain, accepted); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    for (const auto& entry : accepted) {
        if (entry.mRule.mAction == nftables::FWActionEnum::eAccept
            && entry.mRule.mDstAddr == incoming->mRule.mDstAddr) {
            handles.push_back(entry.mHandle);
            break;
        }
    }

    for (const auto& entry : accepted) {
        if (entry.mRule.mAction == nftables::FWActionEnum::eAccept
            && entry.mRule.mSrcAddr == incoming->mRule.mDstAddr) {
            handles.push_back(entry.mHandle);
            break;
        }
    }

    return handles.size() == cNumInstanceHandles ? ErrorEnum::eNone : AOS_ERROR_WRAP(ErrorEnum::eNotFound);
}

void Firewall::DeleteInstanceChain(
    nftables::FWTxnItf& txn, const std::string& chain, const std::vector<nftables::FWRuleHandle>& handles)
{
    txn.DeleteRuleByHandle(mTable, cIngressChain, handles[cIngressHandleIndex]);
    txn.DeleteRuleByHandle(mTable, cEgressChain, handles[cEgressHandleIndex]);
    txn.DeleteRuleByHandle(mTable, cAcceptedChain, handles[cAcceptInHandleIndex]);
    txn.DeleteRuleByHandle(mTable, cAcceptedChain, handles[cAcceptOutHandleIndex]);

    for (const auto& instanceChain : {chain, chain + "_out"}) {
        txn.FlushChain(mTable, instanceChain);
        txn.DeleteChain(mTable, instanceChain);
    }
}

Error Firewall::RemoveInstance(const String& instanceID)
{
    LOG_DBG() << "Remove firewall instance" << Log::Field("instanceID", instanceID);

    const auto chain = ChainName(instanceID);

    std::vector<nftables::FWRuleHandle> jumpHandles;

    {
        std::lock_guard lock {mBatchMutex};

        if (auto it = mInstanceJumps.find(chain); it != mInstanceJumps.end()) {
            jumpHandles = it->second;

            mInstanceJumps.erase(it);
        }
    }

    if (jumpHandles.empty()) {
        if (auto err = FindInstanceRules(chain, jumpHandles); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (jumpHandles.empty()) {
            return ErrorEnum::eNone;
        }
    }

    {
        std::lock_guard lock {mBatchMutex};

        if (mBatchMode && mBatchTxn) {
            DeleteInstanceChain(*mBatchTxn, chain, jumpHandles);

            return ErrorEnum::eNone;
        }
    }

    auto txn = mBackend->NewTxn();

    DeleteInstanceChain(*txn, chain, jumpHandles);

    if (auto err = txn->Commit(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error Firewall::BeginBatch()
{
    LOG_DBG() << "Begin firewall batch";

    std::lock_guard lock {mBatchMutex};

    mBatchTxn = mBackend->NewTxn();

    mBatchChains.clear();
    mAppliedHandles.clear();

    mBatchMode = true;

    return ErrorEnum::eNone;
}

Error Firewall::FlushBatch()
{
    LOG_DBG() << "Flush firewall batch";

    std::unique_ptr<nftables::FWTxnItf> txn;

    {
        std::lock_guard lock {mBatchMutex};

        mBatchMode = false;
        txn        = std::move(mBatchTxn);
    }

    if (!txn) {
        return ErrorEnum::eNone;
    }

    std::vector<nftables::FWListedRule> added;

    const auto err = txn->Commit(added);

    std::lock_guard lock {mBatchMutex};

    if (!err.IsNone()) {
        mBatchChains.clear();

        return AOS_ERROR_WRAP(err);
    }

    std::unordered_map<std::string, std::string> chainsByIP;

    for (const auto& entry : added) {
        mAppliedHandles.insert(entry.mHandle);

        if (entry.mRule.mAction == nftables::FWActionEnum::eJump && !entry.mRule.mDstAddr.empty()) {
            chainsByIP[entry.mRule.mDstAddr]        = entry.mRule.mJumpTarget;
            mInstanceJumps[entry.mRule.mJumpTarget] = std::vector<nftables::FWRuleHandle>(cNumInstanceHandles);
        }
    }

    // Keep the four dispatch handles in ingress, egress, accept-in, accept-out order.
    // All metadata comes from the commit echo; no chain listing is needed here.

    for (const auto& entry : added) {
        const auto& rule = entry.mRule;
        const auto  it   = chainsByIP.find(rule.mDstAddr.empty() ? rule.mSrcAddr : rule.mDstAddr);

        if (it == chainsByIP.end()) {
            continue;
        }

        auto& handles = mInstanceJumps[it->second];

        if (rule.mAction == nftables::FWActionEnum::eJump) {
            handles[rule.mDstAddr.empty() ? cEgressHandleIndex : cIngressHandleIndex] = entry.mHandle;
        } else if (rule.mAction == nftables::FWActionEnum::eAccept) {
            handles[rule.mDstAddr.empty() ? cAcceptOutHandleIndex : cAcceptInHandleIndex] = entry.mHandle;
        }
    }

    return ErrorEnum::eNone;
}

Error Firewall::AbortBatch()
{
    LOG_DBG() << "Abort firewall batch";

    std::lock_guard lock {mBatchMutex};

    mBatchMode = false;

    mBatchTxn.reset();

    mBatchChains.clear();
    mAppliedHandles.clear();

    return ErrorEnum::eNone;
}

Error Firewall::Revert()
{
    LOG_DBG() << "Revert firewall batch";

    std::set<std::string>            chains;
    std::set<nftables::FWRuleHandle> handles;

    {
        std::lock_guard lock {mBatchMutex};

        chains  = std::move(mBatchChains);
        handles = std::move(mAppliedHandles);

        mBatchChains.clear();
        mAppliedHandles.clear();

        for (const auto& chain : chains) {
            mInstanceJumps.erase(chain);
        }
    }

    if (chains.empty() && handles.empty()) {
        return ErrorEnum::eNone;
    }

    auto txn = mBackend->NewTxn();

    // As before, find batch dispatch rules by their recorded handles and targets.
    // Dispatch is now split across three chains; list each once for the whole batch.

    for (const auto* dispatch : {cIngressChain, cEgressChain, cAcceptedChain}) {
        std::vector<nftables::FWListedRule> rules;

        if (auto err = mBackend->ListChainRules(mTable, dispatch, rules); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        for (const auto& entry : rules) {
            auto target = entry.mRule.mJumpTarget;

            if (dispatch == cEgressChain && target.size() >= 4) {
                target.resize(target.size() - 4);
            }

            const bool batchJump = entry.mRule.mAction == nftables::FWActionEnum::eJump && chains.count(target) != 0;

            if (batchJump || handles.count(entry.mHandle) != 0) {
                txn->DeleteRuleByHandle(mTable, dispatch, entry.mHandle);
            }
        }
    }

    for (const auto& chain : chains) {
        for (const auto& instanceChain : {chain, chain + "_out"}) {
            txn->FlushChain(mTable, instanceChain);
            txn->DeleteChain(mTable, instanceChain);
        }
    }

    if (auto err = txn->Commit(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error Firewall::UpdateInstance(const String& instanceID, const InstanceFirewallParams& params)
{
    LOG_DBG() << "Update firewall instance" << Log::Field("instanceID", instanceID);

    if (params.mIP.IsEmpty()) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eInvalidArgument, "instance IP required"));
    }

    const auto chain = ChainName(instanceID);

    std::vector<nftables::FWRuleHandle> oldHandles;

    if (auto err = FindInstanceRules(chain, oldHandles); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (oldHandles.empty()) {
        return AOS_ERROR_WRAP(ErrorEnum::eNotFound);
    }

    auto txn = mBackend->NewTxn();

    DeleteInstanceChain(*txn, chain, oldHandles);

    if (auto err = AppendInstanceChain(*txn, chain, params); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    std::vector<nftables::FWRuleHandle> handles;

    if (auto err = txn->Commit(handles); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (handles.size() >= cNumInstanceHandles) {
        std::lock_guard lock {mBatchMutex};

        mInstanceJumps[chain] = {handles.end() - cNumInstanceHandles, handles.end()};
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

    nftables::FWRule r {};
    r.mSrcAddr = key.first;
    r.mOIFName = key.second;
    r.mOIFNeg  = false;
    r.mAction  = nftables::FWActionEnum::eMasquerade;

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

    std::vector<nftables::FWListedRule> postRules;

    if (auto err = mBackend->ListChainRules(mTable, cPostroutingChain, postRules); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    const auto it = std::find_if(postRules.begin(), postRules.end(), [&key](const nftables::FWListedRule& r) {
        return r.mRule.mAction == nftables::FWActionEnum::eMasquerade && !r.mRule.mOIFNeg
            && r.mRule.mSrcAddr == key.first && r.mRule.mOIFName == key.second;
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
