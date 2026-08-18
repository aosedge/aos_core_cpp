/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_NETWORKMANAGER_FIREWALL_HPP_
#define AOS_SM_NETWORKMANAGER_FIREWALL_HPP_

#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <core/common/tools/noncopyable.hpp>
#include <core/sm/networkmanager/itf/firewall.hpp>

#include <sm/nftables/itf/firewallbackend.hpp>

namespace aos::sm::networkmanager {

/**
 * Native firewall implementation backed by an FWBackendItf.
 */
class Firewall : public FirewallItf, private NonCopyable {
public:
    /**
     * Initializes the firewall.
     *
     * @param backend firewall backend.
     * @return Error.
     */
    Error Init(nftables::FWBackendItf& backend);

    /**
     * Starts the firewall: ensures the table and base chains are in place.
     *
     * @return Error.
     */
    Error Start() override;

    /**
     * Stops the firewall: removes the rules and chains it added.
     *
     * @return Error.
     */
    Error Stop() override;

    /**
     * Removes the instance chains and masquerade rules that are not known.
     *
     * @param knownInstanceIDs instance ids whose chains must be kept.
     * @param knownMasquerades masquerade rules that must be kept.
     * @return Error.
     */
    Error RemoveOrphans(
        const Array<StaticString<cIDLen>>& knownInstanceIDs, const Array<MasqueradeParams>& knownMasquerades) override;

    /**
     * Adds a per-instance chain with input/output rules.
     *
     * @param instanceID instance id.
     * @param params per-instance firewall parameters.
     * @return Error.
     */
    Error AddInstance(const String& instanceID, const InstanceFirewallParams& params) override;

    /**
     * Removes the per-instance chain.
     *
     * @param instanceID instance id.
     * @return Error.
     */
    Error RemoveInstance(const String& instanceID) override;

    /**
     * Atomically replaces the per-instance chain content.
     *
     * @param instanceID instance id.
     * @param params new per-instance firewall parameters.
     * @return Error.
     */
    Error UpdateInstance(const String& instanceID, const InstanceFirewallParams& params) override;

    /**
     * Adds an IPMasq rule.
     *
     * @param subnet source subnet (CIDR).
     * @param outIf output interface.
     * @return Error.
     */
    Error AddMasquerade(const String& subnet, const String& outIf) override;

    /**
     * Removes the IPMasq rule.
     *
     * @param subnet source subnet (CIDR).
     * @param outIf output interface.
     * @return Error.
     */
    Error RemoveMasquerade(const String& subnet, const String& outIf) override;

    /**
     * Begins batch mode: AddInstance/RemoveInstance stage their nft operations
     * into a single shared transaction instead of committing per instance.
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
     * Discards the staged batch and leaves batch mode without applying anything.
     *
     * @return Error.
     */
    Error AbortBatch() override;

    /**
     * Deletes by handle everything the last flushed batch added, together with
     * the instance chains it created.
     *
     * @return Error.
     */
    Error Revert() override;

private:
    static constexpr auto cTableName           = "aos";
    static constexpr auto cForwardChain        = "forward";
    static constexpr auto cPostroutingChain    = "postrouting";
    static constexpr auto cForwardPriority     = 0;
    static constexpr auto cNATPriority         = 100;
    static constexpr auto cInstanceChainPrefix = "instance_";

    static std::string ChainName(const String& instanceID);

    Error CreateSkeleton();
    Error ReconcileArtifacts(const std::vector<nftables::FWListedRule>& forwardRules);
    Error AppendInstanceChain(nftables::FWTxnItf& txn, const std::string& chain, const InstanceFirewallParams& params);
    void  DeleteInstanceChain(
         nftables::FWTxnItf& txn, const std::string& chain, const std::vector<nftables::FWRuleHandle>& jumpHandles);

    const std::string                             mTable {cTableName};
    nftables::FWBackendItf*                       mBackend {};
    std::set<std::pair<std::string, std::string>> mMasqueradeRules;

    std::mutex                          mBatchMutex;
    bool                                mBatchMode {false};
    std::unique_ptr<nftables::FWTxnItf> mBatchTxn;
    std::set<std::string>               mBatchChains;
    std::set<nftables::FWRuleHandle>    mAppliedHandles;

    std::unordered_map<std::string, std::pair<nftables::FWRuleHandle, nftables::FWRuleHandle>> mInstanceJumps;
};

} // namespace aos::sm::networkmanager

#endif
