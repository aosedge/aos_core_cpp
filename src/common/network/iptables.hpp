/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_NETWORK_IPTABLES_HPP_
#define AOS_COMMON_NETWORK_IPTABLES_HPP_

#include <mutex>
#include <string>
#include <vector>

#include <core/common/tools/error.hpp>
#include <core/common/tools/noncopyable.hpp>

#include "itf/iptables.hpp"

namespace aos::common::network {

/**
 * Implementation of iptables.
 *
 */
class IPTables : public IPTablesItf, private NonCopyable {
public:
    /**
     * Constructor.
     *
     * @param table table name.
     */
    explicit IPTables(const std::string& table = "filter");

    /**
     * Appends rule to the chain.
     *
     * @param chain chain name.
     * @param builder rule builder.
     * @return error.
     */
    Error Append(const std::string& chain, const RuleBuilder& builder) override;

    /**
     * Inserts rule to the chain at specified position.
     *
     * @param chain chain name.
     * @param position position.
     * @param builder rule builder.
     * @return error.
     */
    Error Insert(const std::string& chain, unsigned int position, const RuleBuilder& builder) override;

    /**
     * Deletes rule from the chain.
     *
     * @param chain chain name.
     * @param builder rule builder.
     * @return error.
     */
    Error DeleteRule(const std::string& chain, const RuleBuilder& builder) override;

    /**
     * Creates new chain.
     *
     * @param chain chain name.
     * @return error.
     */
    Error NewChain(const std::string& chain) override;

    /**
     * Clears chain.
     *
     * @param chain chain name.
     * @return error.
     */
    Error ClearChain(const std::string& chain) override;

    /**
     * Deletes chain.
     *
     * @param chain chain name.
     * @return error.
     */
    Error DeleteChain(const std::string& chain) override;

    /**
     * Lists all chains.
     *
     * @return list of chains.
     */
    RetWithError<std::vector<std::string>> ListChains() override;

    /**
     * Lists all rules with counters.
     *
     * @param chain chain name.
     * @return list of rules with counters.
     */
    RetWithError<std::vector<std::string>> ListAllRulesWithCounters(const std::string& chain) override;

private:
    void                     ExecuteCommand(const std::string& command) const;
    std::vector<std::string> ExecuteCommandWithOutput(const std::string& command) const;

    std::string mTable;
    std::mutex  mMutex;
};
} // namespace aos::common::network

#endif
