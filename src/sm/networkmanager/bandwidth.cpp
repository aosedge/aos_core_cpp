/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <cstdint>

#include <core/common/tools/logger.hpp>
#include <core/common/tools/memory.hpp>

#include "bandwidth.hpp"

namespace aos::sm::networkmanager {

namespace {

constexpr uint64_t cLimitBurstMultiplier = 4;
constexpr uint64_t cBitsPerByte          = 8;

common::network::TBFParams MakeTBFParams(uint64_t rateBitsPerSec, uint64_t burstBytes)
{
    // BandwidthParams rates are bits/sec; tc / TBF works in bytes/sec.
    const uint64_t rateBytesPerSec = rateBitsPerSec / cBitsPerByte;

    return {rateBytesPerSec, burstBytes, burstBytes * cLimitBurstMultiplier};
}

} // namespace

/***********************************************************************************************************************
 * Public
 **********************************************************************************************************************/

Error Bandwidth::Init(common::network::TCBackendItf& tc, InterfaceFactoryItf& ifFactory, InterfaceManagerItf& ifMgr)
{
    mTC        = &tc;
    mIfFactory = &ifFactory;
    mIfMgr     = &ifMgr;

    return ErrorEnum::eNone;
}

Error Bandwidth::Apply(const String& ifName, const BandwidthParams& params)
{
    if (params.mIngressRate == 0 && params.mEgressRate == 0) {
        return ErrorEnum::eNone;
    }

    LOG_DBG() << "Apply bandwidth" << Log::Field("ifName", ifName) << Log::Field("ingressRate", params.mIngressRate)
              << Log::Field("egressRate", params.mEgressRate);

    Error err;

    if (params.mIngressRate > 0) {
        if (err = mTC->AddRootTBFQDisc(ifName, MakeTBFParams(params.mIngressRate, params.mIngressBurst));
            !err.IsNone()) {
            return AOS_ERROR_WRAP(err);
        }
    }

    auto cleanupIngress = DeferRelease(&err, [this, &ifName, ingressOn = params.mIngressRate > 0](const Error* e) {
        if (e->IsNone() || !ingressOn) {
            return;
        }

        if (auto rb = mTC->DelRootTBFQDisc(ifName); !rb.IsNone()) {
            LOG_ERR() << "Failed to delete root TBF qdisc" << Log::Field(rb);
        }
    });

    if (params.mEgressRate == 0) {
        return ErrorEnum::eNone;
    }

    const auto ifbName = IFBName(ifName);

    if (err = mIfFactory->CreateLink(ifbName, String("ifb")); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    auto cleanupIFB = DeferRelease(&err, [this, &ifbName](const Error* e) {
        if (e->IsNone()) {
            return;
        }

        if (auto rb = mIfMgr->DeleteLink(ifbName); !rb.IsNone()) {
            LOG_ERR() << "Failed to delete IFB" << Log::Field(rb);
        }
    });

    if (err = mIfMgr->SetupLink(ifbName); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (err = mTC->AddIngressQDisc(ifName); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    auto cleanupIngressQDisc = DeferRelease(&err, [this, &ifName](const Error* e) {
        if (e->IsNone()) {
            return;
        }

        if (auto rb = mTC->DelIngressQDisc(ifName); !rb.IsNone()) {
            LOG_ERR() << "Failed to delete ingress qdisc" << Log::Field(rb);
        }
    });

    if (err = mTC->AddIngressMirredFilter(ifName, ifbName); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    if (err = mTC->AddRootTBFQDisc(ifbName, MakeTBFParams(params.mEgressRate, params.mEgressBurst)); !err.IsNone()) {
        return AOS_ERROR_WRAP(err);
    }

    return ErrorEnum::eNone;
}

Error Bandwidth::Clear(const String& ifName)
{
    LOG_DBG() << "Clear bandwidth" << Log::Field("ifName", ifName);

    Error err;

    if (auto rootErr = mTC->DelRootTBFQDisc(ifName); !rootErr.IsNone()) {
        LOG_ERR() << "Failed to delete root TBF qdisc" << Log::Field(rootErr);

        err = AOS_ERROR_WRAP(rootErr);
    }

    if (auto ingErr = mTC->DelIngressQDisc(ifName); !ingErr.IsNone()) {
        LOG_ERR() << "Failed to delete ingress qdisc" << Log::Field(ingErr);

        if (err.IsNone()) {
            err = AOS_ERROR_WRAP(ingErr);
        }
    }

    if (auto ifbErr = mIfMgr->DeleteLink(IFBName(ifName)); !ifbErr.IsNone() && !ifbErr.Is(ErrorEnum::eNotFound)) {
        LOG_ERR() << "Failed to delete IFB" << Log::Field(ifbErr);

        if (err.IsNone()) {
            err = AOS_ERROR_WRAP(ifbErr);
        }
    }

    return err;
}

/***********************************************************************************************************************
 * Private
 **********************************************************************************************************************/

StaticString<cInterfaceLen> Bandwidth::IFBName(const String& hostIfName)
{
    // FNV-1a 32-bit — deterministic so Clear can recompute without persisting the name.
    uint32_t hash = 2166136261u;

    for (size_t i = 0; i < hostIfName.Size(); ++i) {
        hash ^= static_cast<uint8_t>(hostIfName[i]);
        hash *= 16777619u;
    }

    // "ifb-" + 8 hex = 12 chars, within IFNAMSIZ-1 (15).
    StaticString<cInterfaceLen> name;

    name.Format("ifb-%08x", hash);

    return name;
}

} // namespace aos::sm::networkmanager
