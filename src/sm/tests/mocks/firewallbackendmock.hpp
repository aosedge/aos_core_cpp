/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_TESTS_MOCKS_FIREWALLBACKENDMOCK_HPP_
#define AOS_COMMON_TESTS_MOCKS_FIREWALLBACKENDMOCK_HPP_

#include <gmock/gmock.h>

#include <sm/nftables/itf/firewallbackend.hpp>

namespace aos::sm::nftables {

class MockFWTxn : public FWTxnItf {
public:
    MOCK_METHOD(void, AddTable, (const std::string& table), (override));
    MOCK_METHOD(void, DeleteTable, (const std::string& table), (override));
    MOCK_METHOD(void, AddBaseChain, (const FWBaseChain& chain), (override));
    MOCK_METHOD(void, AddChain, (const FWChain& chain), (override));
    MOCK_METHOD(void, FlushChain, (const std::string& table, const std::string& chain), (override));
    MOCK_METHOD(void, DeleteChain, (const std::string& table, const std::string& chain), (override));
    MOCK_METHOD(Error, AddRule, (const std::string& table, const std::string& chain, const FWRule& rule), (override));
    MOCK_METHOD(void, DeleteRuleByHandle, (const std::string& table, const std::string& chain, FWRuleHandle handle),
        (override));
    MOCK_METHOD(Error, Commit, (), (override));
};

class MockFWBackend : public FWBackendItf {
public:
    MOCK_METHOD(std::unique_ptr<FWTxnItf>, NewTxn, (), (override));
    MOCK_METHOD(Error, ListChainRules,
        (const std::string& table, const std::string& chain, std::vector<FWListedRule>& out), (override));
};

} // namespace aos::sm::nftables

#endif
