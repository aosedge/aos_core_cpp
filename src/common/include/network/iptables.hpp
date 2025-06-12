/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef NETWORK_IPTABLES_HPP
#define NETWORK_IPTABLES_HPP

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include <aos/common/tools/error.hpp>
#include <aos/common/tools/noncopyable.hpp>

namespace aos::common::network {

/**
 * Builder for iptables rule.
 *
 */
class RuleBuilder {
public:
    /**
     * Sets source address.
     *
     * @param addr source address.
     */
    RuleBuilder& Source(const std::string& addr);

    /**
     * Sets destination address.
     *
     * @param addr destination address.
     */
    RuleBuilder& Destination(const std::string& addr);

    /**
     * Sets protocol.
     *
     * @param proto protocol.
     */
    RuleBuilder& Protocol(const std::string& proto);

    /**
     * Sets jump target.
     *
     * @param target jump target.
     */
    RuleBuilder& Jump(const std::string& target);

    /**
     * Sets source port.
     *
     * @param port source port.
     */
    RuleBuilder& SourcePort(uint16_t port);

    /**
     * Sets destination port.
     *
     * @param port destination port.
     */
    RuleBuilder& DestinationPort(uint16_t port);

    /**
     * Builds rule string.
     *
     * @return rule string.
     */
    std::string Build() const;

    /**
     * Resets rule builder.
     */
    void Reset();

private:
    std::string mRule;
};

/**
 * Interface for iptables.
 *
 */
class IPTablesItf {
public:
    /**
     * Destructor.
     */
    virtual ~IPTablesItf() = default;

    /**
     * Appends rule to the chain.
     *
     * @param chain chain name.
     * @param builder rule builder.
     * @return error.
     */
    virtual Error Append(const std::string& chain, const RuleBuilder& builder) = 0;

    /**
     * Inserts rule to the chain at specified position.
     *
     * @param chain chain name.
     * @param position position.
     * @param builder rule builder.
     * @return error.
     */
    virtual Error Insert(const std::string& chain, unsigned int position, const RuleBuilder& builder) = 0;

    /**
     * Deletes rule from the chain.
     *
     * @param chain chain name.
     * @param builder rule builder.
     * @return error.
     */
    virtual Error DeleteRule(const std::string& chain, const RuleBuilder& builder) = 0;

    /**
     * Creates new chain.
     *
     * @param chain chain name.
     * @return error.
     */
    virtual Error NewChain(const std::string& chain) = 0;

    /**
     * Clears chain.
     *
     * @param chain chain name.
     * @return error.
     */
    virtual Error ClearChain(const std::string& chain) = 0;

    /**
     * Deletes chain.
     *
     * @param chain chain name.
     * @return error.
     */
    virtual Error DeleteChain(const std::string& chain) = 0;

    /**
     * Lists all chains.
     *
     * @return list of chains.
     */
    virtual RetWithError<std::vector<std::string>> ListChains() = 0;

    /**
     * Lists all rules with counters.
     *
     * @param chain chain name.
     * @return list of rules with counters.
     */
    virtual RetWithError<std::vector<std::string>> ListAllRulesWithCounters(const std::string& chain) = 0;

    /**
     * Creates new rule builder.
     *
     * @return new rule builder.
     */
    RuleBuilder CreateRule() { return RuleBuilder(); }
};

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

#endif // NETWORK_IPTABLES_HPP
