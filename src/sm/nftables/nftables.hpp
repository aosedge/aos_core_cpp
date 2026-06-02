/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_NETWORK_NFTABLES_HPP_
#define AOS_COMMON_NETWORK_NFTABLES_HPP_

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <core/common/tools/error.hpp>
#include <core/common/tools/noncopyable.hpp>

#include "itf/firewallbackend.hpp"

namespace aos::sm::nftables {

/**
 * libnftables-backed FWBackendItf implementation.
 */
class NFTables : public FWBackendItf, private NonCopyable {
public:
    /**
     * Constructor.
     *
     * @param family nftables address family ("inet", "ip", "ip6", ...).
     */
    explicit NFTables(std::string family = "inet");

    /**
     * Begins a new atomic transaction.
     *
     * @return new transaction.
     */
    std::unique_ptr<FWTxnItf> NewTxn() override;

    /**
     * Lists rules in the given chain along with their handles.
     *
     * @param table table the chain belongs to.
     * @param chain chain name.
     * @param[out] out parsed rules.
     * @return error.
     */
    Error ListChainRules(const std::string& table, const std::string& chain, std::vector<FWListedRule>& out) override;

private:
    class NFTxn;

    Error RunBuffer(const std::string& cmd);
    Error RunBufferWithOutput(const std::string& cmd, std::string& output);

    std::string mFamily;
    std::mutex  mMutex;
};

} // namespace aos::sm::nftables

#endif
