/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_NETWORK_ITF_FIREWALLBACKEND_HPP_
#define AOS_COMMON_NETWORK_ITF_FIREWALLBACKEND_HPP_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <core/common/tools/array.hpp>
#include <core/common/tools/enum.hpp>
#include <core/common/tools/error.hpp>

namespace aos::sm::nftables {

using FWRuleHandle = uint64_t;

/**
 * Chain type (netfilter table family the chain belongs to).
 */
class FWChainTypeType {
public:
    enum class Enum {
        eFilter,
        eNAT,
    };

    static const Array<const char* const> GetStrings()
    {
        static const char* const sStrings[] = {"filter", "nat"};

        return Array<const char* const>(sStrings, ArraySize(sStrings));
    };
};

using FWChainTypeEnum = FWChainTypeType::Enum;
using FWChainType     = EnumStringer<FWChainTypeType>;

/**
 * Hook attachment point in the netfilter pipeline.
 */
class FWHookType {
public:
    enum class Enum {
        eForward,
        ePostrouting,
        eInput,
        eOutput,
    };

    static const Array<const char* const> GetStrings()
    {
        static const char* const sStrings[] = {"forward", "postrouting", "input", "output"};

        return Array<const char* const>(sStrings, ArraySize(sStrings));
    };
};

using FWHookEnum = FWHookType::Enum;
using FWHook     = EnumStringer<FWHookType>;

/**
 * Rule action.
 */
class FWActionType {
public:
    enum class Enum {
        eAccept,
        eDrop,
        eJump,
        eMasquerade,
        eReturn,
    };

    static const Array<const char* const> GetStrings()
    {
        static const char* const sStrings[] = {"accept", "drop", "jump", "masquerade", "return"};

        return Array<const char* const>(sStrings, ArraySize(sStrings));
    };
};

using FWActionEnum = FWActionType::Enum;
using FWAction     = EnumStringer<FWActionType>;

/**
 * Base chain — anchored to a netfilter hook with a priority.
 */
struct FWBaseChain {
    std::string mTable;
    std::string mName;
    FWChainType mType;
    FWHook      mHook;
    int         mPriority;
    FWAction    mPolicy;
};

/**
 * Regular (non-base) chain — only reachable via `jump` from another chain.
 */
struct FWChain {
    std::string mTable;
    std::string mName;
};

/**
 * A rule expression. Empty / zero match fields are not added to the rule.
 *
 * When mCounter is true, a `counter` expression is emitted right before the
 * verdict (used by TrafficMonitor to read per-rule byte/packet counts).
 * mCtState, when set (e.g. "established,related" or "invalid"), emits a
 * `ct state` match.
 * When mOIFNeg is true, the mOIFName match is negated (`oifname != "X"`),
 * e.g. masquerade traffic that leaves the host via any interface but the
 * bridge.
 */
struct FWRule {
    std::string mSrcAddr;
    std::string mDstAddr;
    std::string mProto;
    uint16_t    mDstPort {};
    std::string mOIFName;
    FWAction    mAction;
    std::string mJumpTarget;
    bool        mCounter {};
    std::string mCtState {};
    bool        mOIFNeg {};
};

/**
 * A listed rule paired with its handle. Counter values are populated when the
 * underlying rule carries a `counter` expression.
 */
struct FWListedRule {
    FWRule       mRule;
    FWRuleHandle mHandle;
    uint64_t     mBytes {};
    uint64_t     mPackets {};
};

/**
 * Atomic transaction.
 *
 * Operations queue inside the object and are submitted as a single batch on
 * Commit(). Dropping the unique_ptr without calling Commit() discards every
 * queued operation.
 */
class FWTxnItf {
public:
    /**
     * Destructor.
     */
    virtual ~FWTxnItf() = default;

    /**
     * Queues creation of the given table.
     *
     * @param table table name.
     */
    virtual void AddTable(const std::string& table) = 0;

    /**
     * Queues deletion of the given table.
     *
     * @param table table name.
     */
    virtual void DeleteTable(const std::string& table) = 0;

    /**
     * Queues creation of a base chain (attached to a netfilter hook).
     *
     * @param chain base chain definition.
     */
    virtual void AddBaseChain(const FWBaseChain& chain) = 0;

    /**
     * Queues creation of a regular (non-base) chain.
     *
     * @param chain chain definition.
     */
    virtual void AddChain(const FWChain& chain) = 0;

    /**
     * Queues flushing all rules from the given chain.
     *
     * @param table table the chain belongs to.
     * @param chain chain name.
     */
    virtual void FlushChain(const std::string& table, const std::string& chain) = 0;

    /**
     * Queues deletion of the given chain.
     *
     * @param table table the chain belongs to.
     * @param chain chain name.
     */
    virtual void DeleteChain(const std::string& table, const std::string& chain) = 0;

    /**
     * Queues addition of a rule to the given chain. Returns an error when a
     * rule field contains a token unsafe for the backend's command encoding.
     *
     * @param table table the chain belongs to.
     * @param chain chain name.
     * @param rule  rule to add.
     * @return error.
     */
    virtual Error AddRule(const std::string& table, const std::string& chain, const FWRule& rule) = 0;

    /**
     * Queues deletion of a rule identified by its handle.
     *
     * @param table  table the chain belongs to.
     * @param chain  chain name.
     * @param handle rule handle returned by ListChainRules().
     */
    virtual void DeleteRuleByHandle(const std::string& table, const std::string& chain, FWRuleHandle handle) = 0;

    /**
     * Submits the queued batch as a single transaction and clears the queue.
     * The same object may be reused to assemble subsequent batches.
     *
     * @return error.
     */
    virtual Error Commit() = 0;
};

/**
 * Firewall backend.
 */
class FWBackendItf {
public:
    /**
     * Destructor.
     */
    virtual ~FWBackendItf() = default;

    /**
     * Begins a new atomic transaction.
     *
     * @return new transaction.
     */
    virtual std::unique_ptr<FWTxnItf> NewTxn() = 0;

    /**
     * Lists rules in the given chain along with their handles.
     *
     * @param table table the chain belongs to.
     * @param chain chain name.
     * @param[out] out parsed rules.
     * @return error.
     */
    virtual Error ListChainRules(const std::string& table, const std::string& chain, std::vector<FWListedRule>& out)
        = 0;
};

} // namespace aos::sm::nftables

#endif
