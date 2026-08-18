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

Error CheckPortProto(uint16_t port, const String& proto)
{
    if (port == 0) {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eInvalidArgument, "access rule requires a port"));
    }

    const std::string value {proto.CStr()};
    if (!value.empty() && value != "tcp" && value != "udp") {
        return AOS_ERROR_WRAP(Error(ErrorEnum::eInvalidArgument, "unsupported protocol"));
    }

    return ErrorEnum::eNone;
}

std::string ProtoOrDefault(const String& proto, uint16_t port)
{
    // A port match needs a transport protocol; default to tcp (matching the
    // historical CNI/networkmanager convention). Without it an empty proto with
    // a port would widen the rule to all traffic to/from the instance.
    if (port != 0 && proto.IsEmpty()) {
        return "tcp";
    }

    return proto.CStr();
}

Error AppendInstanceRules(
    nftables::FWTxnItf& txn, const std::string& table, const std::string& chain, const InstanceFirewallParams& params)
{
    const std::string instanceIP {params.mIP.CStr()};

    // Instances sharing a network (same subnet) communicate without
    // restrictions: accept intra-subnet traffic before the per-instance access
    // rules, so only cross-network traffic is filtered. The rules sit at the
    // top of the instance chain; same-network flows match here and never reach
    // the terminal drop, while traffic to/from other subnets falls through.
    if (!params.mSubnet.IsEmpty()) {
        const std::string subnet {params.mSubnet.CStr()};

        nftables::FWRule sameNetIn {};
        sameNetIn.mSrcAddr = subnet;
        sameNetIn.mDstAddr = instanceIP;
        sameNetIn.mAction  = nftables::FWActionEnum::eAccept;

        if (auto err = txn.AddRule(table, chain, sameNetIn); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        nftables::FWRule sameNetOut {};
        sameNetOut.mSrcAddr = instanceIP;
        sameNetOut.mDstAddr = subnet;
        sameNetOut.mAction  = nftables::FWActionEnum::eAccept;

        if (auto err = txn.AddRule(table, chain, sameNetOut); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    for (const auto& in : params.mInput) {
        uint16_t port = 0;
        Error    err;

        Tie(port, err) = ParsePort(in.mPort);
        if (!err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        // An input entry requires a port and only tcp/udp are supported (an
        // empty protocol defaults to tcp). Matches the aos_cni_firewall plugin.
        if (err = CheckPortProto(port, in.mProtocol); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        nftables::FWRule r {};

        r.mDstAddr = instanceIP;
        r.mProto   = ProtoOrDefault(in.mProtocol, port);
        r.mDstPort = port;
        r.mAction  = nftables::FWActionEnum::eAccept;

        if (err = txn.AddRule(table, chain, r); !err.IsNone()) {
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

        // An output entry must name a destination; an empty one collapses to a
        // bare ip saddr <instance> accept that opens all egress and bypasses
        // the AllowPublic terminal verdict. It is validated as strictly as an
        // input entry: a destination IP and port, with tcp/udp (empty -> tcp).
        if (out.mDstIP.IsEmpty()) {
            return AOS_ERROR_WRAP(Error(ErrorEnum::eInvalidArgument, "output access requires a destination IP"));
        }

        if (err = CheckPortProto(port, out.mProto); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        if (!out.mSrcIP.IsEmpty() && std::string {out.mSrcIP.CStr()} != instanceIP) {
            LOG_WRN() << "Output rule mSrcIP overridden by instance IP" << Log::Field("srcIP", out.mSrcIP)
                      << Log::Field("instanceIP", params.mIP);
        }

        nftables::FWRule r {};

        r.mSrcAddr = instanceIP;
        r.mDstAddr = out.mDstIP.CStr();
        r.mProto   = ProtoOrDefault(out.mProto, port);
        r.mDstPort = port;
        r.mAction  = nftables::FWActionEnum::eAccept;

        if (err = txn.AddRule(table, chain, r); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    nftables::FWRule terminalIn {};
    terminalIn.mDstAddr = instanceIP;
    terminalIn.mAction  = nftables::FWActionEnum::eDrop;

    if (auto err = txn.AddRule(table, chain, terminalIn); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    nftables::FWRule terminalOut {};
    terminalOut.mSrcAddr = instanceIP;
    terminalOut.mAction  = params.mAllowPublic ? nftables::FWActionEnum::eAccept : nftables::FWActionEnum::eDrop;

    if (auto err = txn.AddRule(table, chain, terminalOut); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
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

    if (auto err = mBackend->ListChainRules(mTable, cForwardChain, forwardRules); !err.IsNone()) {
        return ErrorEnum::eNone;
    }

    std::set<std::string> knownChains;

    for (const auto& instanceID : knownInstanceIDs) {
        knownChains.emplace(ChainName(instanceID));
    }

    std::vector<nftables::FWRuleHandle> jumpHandles;
    std::set<std::string>               orphanChains;

    for (const auto& r : forwardRules) {
        if (r.mRule.mAction != nftables::FWActionEnum::eJump || r.mRule.mJumpTarget.rfind(cInstanceChainPrefix, 0) != 0
            || knownChains.count(r.mRule.mJumpTarget) != 0) {
            continue;
        }

        jumpHandles.push_back(r.mHandle);
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

        if (known.count(key) != 0 && mMasqueradeRules.insert(key).second) {
            continue;
        }

        masqueradeHandles.push_back(r.mHandle);
    }

    if (jumpHandles.empty() && orphanChains.empty() && masqueradeHandles.empty()) {
        return ErrorEnum::eNone;
    }

    auto txn = mBackend->NewTxn();

    for (const auto handle : jumpHandles) {
        txn->DeleteRuleByHandle(mTable, cForwardChain, handle);
    }

    for (const auto& chain : orphanChains) {
        txn->FlushChain(mTable, chain);
        txn->DeleteChain(mTable, chain);
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
    if (auto err = mBackend->ListChainRules(mTable, cForwardChain, forwardRules); !err.IsNone()) {
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
    auto txn = mBackend->NewTxn();

    txn->AddTable(mTable);

    txn->AddBaseChain({mTable, cForwardChain, nftables::FWChainTypeEnum::eFilter, nftables::FWHookEnum::eForward,
        cForwardPriority, nftables::FWActionEnum::eDrop});

    txn->AddBaseChain({mTable, cPostroutingChain, nftables::FWChainTypeEnum::eNAT, nftables::FWHookEnum::ePostrouting,
        cNATPriority, nftables::FWActionEnum::eAccept});

    // Connection tracking gates the per-instance access rules: drop garbage
    // early and let reply traffic of allowed flows back in, so the access
    // rules only need to describe connection initiation.
    nftables::FWRule ctInvalid {};
    ctInvalid.mCtState = "invalid";
    ctInvalid.mAction  = nftables::FWActionEnum::eDrop;

    if (auto err = txn->AddRule(mTable, cForwardChain, ctInvalid); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    nftables::FWRule ctEstablished {};
    ctEstablished.mCtState = "established,related";
    ctEstablished.mAction  = nftables::FWActionEnum::eAccept;

    if (auto err = txn->AddRule(mTable, cForwardChain, ctEstablished); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return txn->Commit();
}

Error Firewall::ReconcileArtifacts(const std::vector<nftables::FWListedRule>& forwardRules)
{
    // Every jump in the forward chain is ours (the base chain otherwise holds
    // only ct rules); the targets name the instance chains to drop.
    std::vector<nftables::FWRuleHandle> jumpHandles;
    std::set<std::string>               instanceChains;

    for (const auto& r : forwardRules) {
        if (r.mRule.mAction == nftables::FWActionEnum::eJump
            && r.mRule.mJumpTarget.rfind(cInstanceChainPrefix, 0) == 0) {
            jumpHandles.push_back(r.mHandle);
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

    if (jumpHandles.empty() && instanceChains.empty() && masqueradeHandles.empty()) {
        return ErrorEnum::eNone;
    }

    auto txn = mBackend->NewTxn();

    for (const auto handle : jumpHandles) {
        txn->DeleteRuleByHandle(mTable, cForwardChain, handle);
    }

    for (const auto& chain : instanceChains) {
        txn->FlushChain(mTable, chain);
        txn->DeleteChain(mTable, chain);
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

    if (handles.size() >= 2) {
        std::lock_guard lock {mBatchMutex};

        mInstanceJumps[chain] = {handles[handles.size() - 2], handles[handles.size() - 1]};
    }

    return ErrorEnum::eNone;
}

Error Firewall::AppendInstanceChain(
    nftables::FWTxnItf& txn, const std::string& chain, const InstanceFirewallParams& params)
{
    txn.AddChain({mTable, chain});

    if (auto err = AppendInstanceRules(txn, mTable, chain, params); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    nftables::FWRule jumpIn {};
    jumpIn.mDstAddr    = params.mIP.CStr();
    jumpIn.mAction     = nftables::FWActionEnum::eJump;
    jumpIn.mJumpTarget = chain;

    if (auto err = txn.AddRule(mTable, cForwardChain, jumpIn); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    nftables::FWRule jumpOut {};
    jumpOut.mSrcAddr    = params.mIP.CStr();
    jumpOut.mAction     = nftables::FWActionEnum::eJump;
    jumpOut.mJumpTarget = chain;

    if (auto err = txn.AddRule(mTable, cForwardChain, jumpOut); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

void Firewall::DeleteInstanceChain(
    nftables::FWTxnItf& txn, const std::string& chain, const std::vector<nftables::FWRuleHandle>& jumpHandles)
{
    for (const auto handle : jumpHandles) {
        txn.DeleteRuleByHandle(mTable, cForwardChain, handle);
    }

    txn.FlushChain(mTable, chain);
    txn.DeleteChain(mTable, chain);
}

Error Firewall::RemoveInstance(const String& instanceID)
{
    LOG_DBG() << "Remove firewall instance" << Log::Field("instanceID", instanceID);

    const auto chain = ChainName(instanceID);

    std::vector<nftables::FWRuleHandle> jumpHandles;

    {
        std::lock_guard lock {mBatchMutex};

        if (auto it = mInstanceJumps.find(chain); it != mInstanceJumps.end()) {
            jumpHandles = {it->second.first, it->second.second};

            mInstanceJumps.erase(it);
        }
    }

    if (jumpHandles.empty()) {
        std::vector<nftables::FWListedRule> forwardRules;

        if (auto err = mBackend->ListChainRules(mTable, cForwardChain, forwardRules); !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }

        for (const auto& r : forwardRules) {
            if (r.mRule.mAction == nftables::FWActionEnum::eJump && r.mRule.mJumpTarget == chain) {
                jumpHandles.push_back(r.mHandle);
            }
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

    std::unordered_map<std::string, std::vector<nftables::FWRuleHandle>> jumpsByChain;

    for (const auto& r : added) {
        mAppliedHandles.insert(r.mHandle);

        if (r.mRule.mAction == nftables::FWActionEnum::eJump) {
            jumpsByChain[r.mRule.mJumpTarget].push_back(r.mHandle);
        }
    }

    for (const auto& [chain, hs] : jumpsByChain) {
        if (hs.size() >= 2) {
            mInstanceJumps[chain] = {hs[hs.size() - 2], hs[hs.size() - 1]};
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

    std::vector<nftables::FWListedRule> forwardRules;

    if (auto err = mBackend->ListChainRules(mTable, cForwardChain, forwardRules); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    auto txn = mBackend->NewTxn();

    for (const auto& r : forwardRules) {
        const bool batchJump
            = r.mRule.mAction == nftables::FWActionEnum::eJump && chains.count(r.mRule.mJumpTarget) != 0;

        if (batchJump || handles.count(r.mHandle) != 0) {
            txn->DeleteRuleByHandle(mTable, cForwardChain, r.mHandle);
        }
    }

    for (const auto& chain : chains) {
        txn->FlushChain(mTable, chain);
        txn->DeleteChain(mTable, chain);
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

    std::vector<nftables::FWListedRule> forwardRules;

    if (auto err = mBackend->ListChainRules(mTable, cForwardChain, forwardRules); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    auto txn = mBackend->NewTxn();

    txn->FlushChain(mTable, chain);

    if (auto err = AppendInstanceRules(*txn, mTable, chain, params); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    // Re-point the parent jumps at the current IP: the child chain now matches
    // params.mIP, so stale jumps for a previous IP would bypass the new policy.
    for (const auto& r : forwardRules) {
        if (r.mRule.mAction == nftables::FWActionEnum::eJump && r.mRule.mJumpTarget == chain) {
            txn->DeleteRuleByHandle(mTable, cForwardChain, r.mHandle);
        }
    }

    nftables::FWRule jumpIn {};
    jumpIn.mDstAddr    = params.mIP.CStr();
    jumpIn.mAction     = nftables::FWActionEnum::eJump;
    jumpIn.mJumpTarget = chain;

    if (auto err = txn->AddRule(mTable, cForwardChain, jumpIn); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    nftables::FWRule jumpOut {};
    jumpOut.mSrcAddr    = params.mIP.CStr();
    jumpOut.mAction     = nftables::FWActionEnum::eJump;
    jumpOut.mJumpTarget = chain;

    if (auto err = txn->AddRule(mTable, cForwardChain, jumpOut); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    std::vector<nftables::FWRuleHandle> handles;

    if (auto err = txn->Commit(handles); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (handles.size() >= 2) {
        std::lock_guard lock {mBatchMutex};

        mInstanceJumps[chain] = {handles[handles.size() - 2], handles[handles.size() - 1]};
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
    r.mOIFNeg  = true;
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
        return r.mRule.mAction == nftables::FWActionEnum::eMasquerade && r.mRule.mSrcAddr == key.first
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
