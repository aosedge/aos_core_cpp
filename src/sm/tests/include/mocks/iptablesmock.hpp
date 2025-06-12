/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IPTABLESMOCK_HPP
#define IPTABLESMOCK_HPP

#include <gmock/gmock.h>

#include <network/iptables.hpp>

namespace aos::common::network {

class MockIPTables : public IPTablesItf {
public:
    MOCK_METHOD(Error, NewChain, (const std::string& chain), (override));
    MOCK_METHOD(Error, DeleteChain, (const std::string& chain), (override));
    MOCK_METHOD(Error, ClearChain, (const std::string& chain), (override));
    MOCK_METHOD(Error, Insert, (const std::string& chain, unsigned int pos, const RuleBuilder& builder), (override));
    MOCK_METHOD(Error, Append, (const std::string& chain, const RuleBuilder& builder), (override));
    MOCK_METHOD(Error, DeleteRule, (const std::string& chain, const RuleBuilder& builder), (override));
    MOCK_METHOD(RetWithError<std::vector<std::string>>, ListChains, (), (override));
    MOCK_METHOD(
        RetWithError<std::vector<std::string>>, ListAllRulesWithCounters, (const std::string& chain), (override));
};

} // namespace aos::common::network

#endif // NETWORKMANAGERMOCK_HPP
