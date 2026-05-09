/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <core/common/tools/logger.hpp>

#include "firewall.hpp"

namespace aos::sm::networkmanager {

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error Firewall::Init(common::network::FWBackendItf& backend)
{
    mBackend = &backend;

    return ErrorEnum::eNone;
}

Error Firewall::Start()
{
    LOG_DBG() << "Start firewall";

    auto txn = mBackend->NewTxn();

    txn->DeleteTable(mTable);

    if (auto err = txn->Commit(); !err.IsNone()) {
        LOG_DBG() << "Stale table cleanup skipped" << Log::Field(err);
    }

    txn->AddTable(mTable);
    txn->AddBaseChain({mTable, cForwardChain, common::network::FWChainTypeEnum::eFilter,
        common::network::FWHookEnum::eForward, cForwardPriority});
    txn->AddBaseChain({mTable, cPostroutingChain, common::network::FWChainTypeEnum::eNAT,
        common::network::FWHookEnum::ePostrouting, cNATPriority});

    if (auto err = txn->Commit(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    mMasqueradeRules.clear();

    return ErrorEnum::eNone;
}

Error Firewall::Stop()
{
    LOG_DBG() << "Stop firewall";

    auto txn = mBackend->NewTxn();

    txn->DeleteTable(mTable);

    if (auto err = txn->Commit(); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    mMasqueradeRules.clear();

    return ErrorEnum::eNone;
}

Error Firewall::AddInstance(
    [[maybe_unused]] const String& instanceID, [[maybe_unused]] const InstanceFirewallParams& params)
{
    return ErrorEnum::eNotSupported;
}

Error Firewall::RemoveInstance([[maybe_unused]] const String& instanceID)
{
    return ErrorEnum::eNotSupported;
}

Error Firewall::UpdateInstance(
    [[maybe_unused]] const String& instanceID, [[maybe_unused]] const InstanceFirewallParams& params)
{
    return ErrorEnum::eNotSupported;
}

Error Firewall::AddMasquerade([[maybe_unused]] const String& subnet, [[maybe_unused]] const String& outIf)
{
    return ErrorEnum::eNotSupported;
}

Error Firewall::RemoveMasquerade([[maybe_unused]] const String& subnet, [[maybe_unused]] const String& outIf)
{
    return ErrorEnum::eNotSupported;
}

} // namespace aos::sm::networkmanager
