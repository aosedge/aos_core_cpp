/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_SM_NETWORKMANAGER_TESTS_STUBS_FIREWALLBACKEND_HPP_
#define AOS_SM_NETWORKMANAGER_TESTS_STUBS_FIREWALLBACKEND_HPP_

#include <algorithm>
#include <functional>
#include <map>
#include <stdexcept>

#include <arpa/inet.h>

#include <sm/nftables/itf/firewallbackend.hpp>

namespace aos::sm::networkmanager::tests {

using namespace nftables;

/**
 * Packet fields used to evaluate firewall rules.
 */
struct Packet {
    std::string mSrc;
    std::string mDst;
    std::string mProto {"udp"};
    uint16_t    mPort {7410};
    std::string mState {"new"};
};

/**
 * Models nft jump/return and base-chain policy for complete packet-path tests.
 */
class FirewallBackend : public FWBackendItf {
public:
    /**
     * Committed chains, policies and rule handles.
     */
    struct State {
        std::map<std::string, std::vector<FWListedRule>> mChains;
        std::map<std::string, FWActionEnum>              mPolicies;
        FWRuleHandle                                     mNextHandle {1};
    };

    /**
     * Staged firewall transaction.
     */
    class Txn : public FWTxnItf {
    public:
        /**
         * Creates a staged transaction.
         *
         * @param backend owning test backend.
         */
        explicit Txn(FirewallBackend& backend)
            : mBackend(backend)
        {
        }

        /**
         * @copydoc FWTxnItf::AddTable
         */
        void AddTable(const std::string&) override { }

        /**
         * @copydoc FWTxnItf::DeleteTable
         */
        void DeleteTable(const std::string&) override
        {
            mOperations.push_back([](State& state) { state = {}; });
        }

        /**
         * @copydoc FWTxnItf::AddBaseChain
         */
        void AddBaseChain(const FWBaseChain& chain) override
        {
            mOperations.push_back([chain](State& state) {
                state.mChains.try_emplace(chain.mName);
                state.mPolicies[chain.mName] = chain.mPolicy.GetValue();
            });
        }

        /**
         * @copydoc FWTxnItf::AddChain
         */
        void AddChain(const FWChain& chain) override
        {
            mOperations.push_back([chain](State& state) { state.mChains.try_emplace(chain.mName); });
        }

        /**
         * @copydoc FWTxnItf::FlushChain
         */
        void FlushChain(const std::string&, const std::string& chain) override
        {
            mOperations.push_back([chain](State& state) { state.mChains.at(chain).clear(); });
        }

        /**
         * @copydoc FWTxnItf::DeleteChain
         */
        void DeleteChain(const std::string&, const std::string& chain) override
        {
            mOperations.push_back([chain](State& state) {
                for (const auto& [_, rules] : state.mChains) {
                    for (const auto& entry : rules) {
                        if (entry.mRule.mAction == FWActionEnum::eJump && entry.mRule.mJumpTarget == chain) {
                            throw std::runtime_error("chain is still referenced");
                        }
                    }
                }

                if (!state.mChains.at(chain).empty()) {
                    throw std::runtime_error("chain is not empty");
                }

                state.mChains.erase(chain);
            });
        }

        /**
         * @copydoc FWTxnItf::AddRule
         */
        Error AddRule(const std::string&, const std::string& chain, const FWRule& rule) override
        {
            if (mBackend.mFailAdd) {
                return ErrorEnum::eRuntime;
            }

            mOperations.push_back([chain, rule](State& state) {
                if (rule.mAction == FWActionEnum::eJump && state.mChains.count(rule.mJumpTarget) == 0) {
                    throw std::runtime_error("jump target missing");
                }

                state.mChains.at(chain).push_back({rule, state.mNextHandle++});
            });

            return ErrorEnum::eNone;
        }

        /**
         * @copydoc FWTxnItf::DeleteRuleByHandle
         */
        void DeleteRuleByHandle(const std::string&, const std::string& chain, FWRuleHandle handle) override
        {
            mOperations.push_back([chain, handle](State& state) {
                auto&      rules = state.mChains.at(chain);
                const auto found = std::find_if(
                    rules.begin(), rules.end(), [handle](const auto& entry) { return entry.mHandle == handle; });

                if (found == rules.end()) {
                    throw std::runtime_error("rule missing");
                }

                rules.erase(found);
            });
        }

        /**
         * @copydoc FWTxnItf::Commit
         */
        Error Commit() override
        {
            ++mBackend.mCommits;

            if (mBackend.mFailCommit) {
                return ErrorEnum::eRuntime;
            }

            auto state = mBackend.mState;

            try {
                for (const auto& operation : mOperations) {
                    operation(state);
                }
            } catch (const std::exception& e) {
                return Error(ErrorEnum::eRuntime, e.what());
            }

            mBackend.mState = std::move(state);
            mOperations.clear();

            return ErrorEnum::eNone;
        }

        /**
         * @copydoc FWTxnItf::Commit
         */
        Error Commit(std::vector<FWRuleHandle>& handles) override
        {
            const auto first = mBackend.mState.mNextHandle;
            const auto err   = Commit();

            handles.clear();

            if (err.IsNone()) {
                for (const auto& [_, entries] : mBackend.mState.mChains) {
                    for (const auto& entry : entries) {
                        if (entry.mHandle >= first) {
                            handles.push_back(entry.mHandle);
                        }
                    }
                }

                std::sort(handles.begin(), handles.end());
            }

            return err;
        }

        /**
         * @copydoc FWTxnItf::Commit
         */
        Error Commit(std::vector<FWListedRule>& rules) override
        {
            const auto first = mBackend.mState.mNextHandle;
            const auto err   = Commit();

            rules.clear();

            if (err.IsNone()) {
                for (const auto& [_, entries] : mBackend.mState.mChains) {
                    for (const auto& entry : entries) {
                        if (entry.mHandle >= first
                            && (entry.mRule.mAction == FWActionEnum::eJump
                                || entry.mRule.mAction == FWActionEnum::eAccept)) {
                            rules.push_back(entry);
                        }
                    }
                }
            }

            return err;
        }

    private:
        FirewallBackend&                         mBackend;
        std::vector<std::function<void(State&)>> mOperations;
    };

    /**
     * @copydoc FWBackendItf::NewTxn
     */
    std::unique_ptr<FWTxnItf> NewTxn() override { return std::make_unique<Txn>(*this); }

    /**
     * @copydoc FWBackendItf::ListChainRules
     */
    Error ListChainRules(const std::string&, const std::string& chain, std::vector<FWListedRule>& rules) override
    {
        ++mLists;

        if (chain == mFailList) {
            return ErrorEnum::eRuntime;
        }

        const auto it = mState.mChains.find(chain);

        if (it == mState.mChains.end()) {
            return ErrorEnum::eNotFound;
        }

        rules = it->second;

        return ErrorEnum::eNone;
    }

    /**
     * Evaluates the complete forwarding path.
     *
     * @param packet packet to evaluate.
     * @return bool whether forwarding accepts the packet.
     */
    bool Accepts(const Packet& packet) const { return Evaluate("forward", packet) == FWActionEnum::eAccept; }

    State       mState;
    bool        mFailCommit {};
    bool        mFailAdd {};
    std::string mFailList;
    size_t      mCommits {};
    size_t      mLists {};

private:
    static bool AddressMatches(const std::string& expression, const std::string& address)
    {
        if (expression.empty()) {
            return true;
        }

        const auto slash  = expression.find('/');
        const auto prefix = slash == std::string::npos ? 32 : std::stoi(expression.substr(slash + 1));
        in_addr    expected {}, actual {};

        if (inet_pton(AF_INET, expression.substr(0, slash).c_str(), &expected) != 1
            || inet_pton(AF_INET, address.c_str(), &actual) != 1) {
            return false;
        }

        const uint32_t mask = prefix == 0 ? 0 : (0xffffffffu << (32 - prefix));

        return (ntohl(expected.s_addr) & mask) == (ntohl(actual.s_addr) & mask);
    }

    FWActionEnum Evaluate(const std::string& chain, const Packet& packet, size_t depth = 0) const
    {
        if (depth > 32) {
            throw std::runtime_error("jump loop");
        }

        for (const auto& entry : mState.mChains.at(chain)) {
            const auto& rule = entry.mRule;

            if (!AddressMatches(rule.mSrcAddr, packet.mSrc) || !AddressMatches(rule.mDstAddr, packet.mDst)
                || (!rule.mProto.empty() && rule.mProto != packet.mProto)
                || (rule.mDstPort != 0
                    && (packet.mPort < rule.mDstPort || packet.mPort > std::max(rule.mDstPort, rule.mDstPortEnd)))
                || (!rule.mCtState.empty() && rule.mCtState.find(packet.mState) == std::string::npos)) {
                continue;
            }

            if (rule.mAction == FWActionEnum::eJump) {
                const auto verdict = Evaluate(rule.mJumpTarget, packet, depth + 1);

                if (verdict == FWActionEnum::eReturn) {
                    continue;
                }

                return verdict;
            }

            return rule.mAction.GetValue();
        }

        const auto policy = mState.mPolicies.find(chain);

        return policy == mState.mPolicies.end() ? FWActionEnum::eReturn : policy->second;
    }
};

} // namespace aos::sm::networkmanager::tests

#endif
