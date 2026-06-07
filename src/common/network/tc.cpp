/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>

#include <linux/if_ether.h>
#include <linux/pkt_cls.h>
#include <linux/pkt_sched.h>

#include <netlink/cache.h>
#include <netlink/errno.h>
#include <netlink/netlink.h>
#include <netlink/route/act/mirred.h>
#include <netlink/route/action.h>
#include <netlink/route/classifier.h>
#include <netlink/route/cls/matchall.h>
#include <netlink/route/link.h>
#include <netlink/route/qdisc.h>
#include <netlink/route/qdisc/tbf.h>
#include <netlink/route/tc.h>

#include <core/common/tools/logger.hpp>

#include "tc.hpp"
#include "utils.hpp"

namespace aos::common::network {

namespace {

constexpr uint32_t cRootHandle    = 0x00010000u; // TC_HANDLE(1, 0)
constexpr uint32_t cIngressParent = 0xFFFFFFF1u; // TC_H_INGRESS
constexpr uint32_t cIngressHandle = 0xFFFF0000u; // TC_H_MAKE(TC_H_INGRESS, 0)
constexpr uint16_t cFilterPrio    = 1;

using QDiscDeleter = std::function<void(rtnl_qdisc*)>;
using ClsDeleter   = std::function<void(rtnl_cls*)>;
using ActDeleter   = std::function<void(rtnl_act*)>;
using LinkDeleter  = std::function<void(rtnl_link*)>;
using CacheDeleter = std::function<void(nl_cache*)>;

using UniqueQDisc = std::unique_ptr<rtnl_qdisc, QDiscDeleter>;
using UniqueCls   = std::unique_ptr<rtnl_cls, ClsDeleter>;
using UniqueAct   = std::unique_ptr<rtnl_act, ActDeleter>;
using UniqueLink  = std::unique_ptr<rtnl_link, LinkDeleter>;
using UniqueCache = std::unique_ptr<nl_cache, CacheDeleter>;

// Casts a concrete libnl route object (qdisc/cls/act) to its rtnl_tc base.
// Equivalent to libnl's TC_CAST macro but without a C-style cast.
inline rtnl_tc* AsTC(void* obj)
{
    return static_cast<rtnl_tc*>(obj);
}

RetWithError<int> LookupIfIndex(nl_sock* sock, const String& ifName)
{
    nl_cache* cacheRaw {};

    if (auto err = rtnl_link_alloc_cache(sock, AF_UNSPEC, &cacheRaw); err < 0) {
        return {0, NLToAosErr(err, "failed to allocate link cache")};
    }

    UniqueCache cache(cacheRaw, nl_cache_free);

    auto link = UniqueLink(rtnl_link_get_by_name(cache.get(), ifName.CStr()), rtnl_link_put);
    if (!link) {
        return {0, Error(ErrorEnum::eNotFound, "interface not found")};
    }

    return {rtnl_link_get_ifindex(link.get()), ErrorEnum::eNone};
}

bool IsNotFound(int nlErr)
{
    return nlErr == -NLE_OBJ_NOTFOUND || nlErr == -NLE_NODEV;
}

bool FitsInTCParam(uint64_t value)
{
    return value <= static_cast<uint64_t>(std::numeric_limits<int>::max());
}

} // namespace

Error TC::AddRootTBFQDisc(const String& ifName, const TBFParams& params)
{
    LOG_DBG() << "Add root TBF qdisc" << Log::Field("ifName", ifName) << Log::Field("rate", params.mRate)
              << Log::Field("burst", params.mBurst) << Log::Field("limit", params.mLimit);

    if (!FitsInTCParam(params.mRate) || !FitsInTCParam(params.mBurst) || !FitsInTCParam(params.mLimit)) {
        return Error(ErrorEnum::eInvalidArgument, "TBF parameter exceeds tc range");
    }

    auto [sock, sockErr] = CreateNetlinkSocket();
    if (!sockErr.IsNone()) {
        return sockErr;
    }

    auto [ifIndex, indexErr] = LookupIfIndex(sock.get(), ifName);
    if (!indexErr.IsNone()) {
        return indexErr;
    }

    auto qdisc = UniqueQDisc(rtnl_qdisc_alloc(), rtnl_qdisc_put);
    if (!qdisc) {
        return NLToAosErr(errno, "failed to allocate qdisc");
    }

    rtnl_tc_set_ifindex(AsTC(qdisc.get()), ifIndex);
    rtnl_tc_set_parent(AsTC(qdisc.get()), TC_H_ROOT);
    rtnl_tc_set_handle(AsTC(qdisc.get()), cRootHandle);

    if (auto err = rtnl_tc_set_kind(AsTC(qdisc.get()), "tbf"); err < 0) {
        return NLToAosErr(err, "failed to set qdisc kind tbf");
    }

    rtnl_qdisc_tbf_set_limit(qdisc.get(), static_cast<int>(params.mLimit));
    rtnl_qdisc_tbf_set_rate(qdisc.get(), static_cast<int>(params.mRate), static_cast<int>(params.mBurst), 0);

    if (auto err = rtnl_qdisc_add(sock.get(), qdisc.get(), NLM_F_CREATE | NLM_F_REPLACE); err < 0) {
        return NLToAosErr(err, "failed to add root TBF qdisc");
    }

    return ErrorEnum::eNone;
}

Error TC::DelRootTBFQDisc(const String& ifName)
{
    LOG_DBG() << "Delete root TBF qdisc" << Log::Field("ifName", ifName);

    auto [sock, sockErr] = CreateNetlinkSocket();
    if (!sockErr.IsNone()) {
        return sockErr;
    }

    auto [ifIndex, indexErr] = LookupIfIndex(sock.get(), ifName);
    if (!indexErr.IsNone()) {
        if (indexErr.Is(ErrorEnum::eNotFound)) {
            return ErrorEnum::eNone;
        }

        return indexErr;
    }

    nl_cache* cacheRaw {};

    if (auto err = rtnl_qdisc_alloc_cache(sock.get(), &cacheRaw); err < 0) {
        return NLToAosErr(err, "failed to allocate qdisc cache");
    }

    UniqueCache cache(cacheRaw, nl_cache_free);

    auto root = UniqueQDisc(rtnl_qdisc_get_by_parent(cacheRaw, ifIndex, TC_H_ROOT), rtnl_qdisc_put);
    if (!root) {
        return ErrorEnum::eNone;
    }

    // Only remove the root qdisc when it is the TBF installed by Apply; the
    // default qdisc or any other shaping is left untouched.
    const char* kind = rtnl_tc_get_kind(AsTC(root.get()));
    if (kind == nullptr || std::strcmp(kind, "tbf") != 0) {
        return ErrorEnum::eNone;
    }

    if (auto err = rtnl_qdisc_delete(sock.get(), root.get()); err < 0 && !IsNotFound(err)) {
        return NLToAosErr(err, "failed to delete root TBF qdisc");
    }

    return ErrorEnum::eNone;
}

Error TC::AddIngressQDisc(const String& ifName)
{
    LOG_DBG() << "Add ingress qdisc" << Log::Field("ifName", ifName);

    auto [sock, sockErr] = CreateNetlinkSocket();
    if (!sockErr.IsNone()) {
        return sockErr;
    }

    auto [ifIndex, indexErr] = LookupIfIndex(sock.get(), ifName);
    if (!indexErr.IsNone()) {
        return indexErr;
    }

    auto qdisc = UniqueQDisc(rtnl_qdisc_alloc(), rtnl_qdisc_put);
    if (!qdisc) {
        return NLToAosErr(errno, "failed to allocate qdisc");
    }

    rtnl_tc_set_ifindex(AsTC(qdisc.get()), ifIndex);
    rtnl_tc_set_parent(AsTC(qdisc.get()), cIngressParent);
    rtnl_tc_set_handle(AsTC(qdisc.get()), cIngressHandle);

    if (auto err = rtnl_tc_set_kind(AsTC(qdisc.get()), "ingress"); err < 0) {
        return NLToAosErr(err, "failed to set qdisc kind ingress");
    }

    if (auto err = rtnl_qdisc_add(sock.get(), qdisc.get(), NLM_F_CREATE | NLM_F_EXCL); err < 0) {
        return NLToAosErr(err, "failed to add ingress qdisc");
    }

    return ErrorEnum::eNone;
}

Error TC::DelIngressQDisc(const String& ifName)
{
    LOG_DBG() << "Delete ingress qdisc" << Log::Field("ifName", ifName);

    auto [sock, sockErr] = CreateNetlinkSocket();
    if (!sockErr.IsNone()) {
        return sockErr;
    }

    auto [ifIndex, indexErr] = LookupIfIndex(sock.get(), ifName);
    if (!indexErr.IsNone()) {
        if (indexErr.Is(ErrorEnum::eNotFound)) {
            return ErrorEnum::eNone;
        }

        return indexErr;
    }

    auto qdisc = UniqueQDisc(rtnl_qdisc_alloc(), rtnl_qdisc_put);
    if (!qdisc) {
        return NLToAosErr(errno, "failed to allocate qdisc");
    }

    rtnl_tc_set_ifindex(AsTC(qdisc.get()), ifIndex);
    rtnl_tc_set_parent(AsTC(qdisc.get()), cIngressParent);
    rtnl_tc_set_handle(AsTC(qdisc.get()), cIngressHandle);

    if (auto err = rtnl_qdisc_delete(sock.get(), qdisc.get()); err < 0 && !IsNotFound(err)) {
        return NLToAosErr(err, "failed to delete ingress qdisc");
    }

    return ErrorEnum::eNone;
}

Error TC::AddIngressMirredFilter(const String& srcIfName, const String& dstIfName)
{
    LOG_DBG() << "Add ingress mirred filter" << Log::Field("srcIfName", srcIfName)
              << Log::Field("dstIfName", dstIfName);

    auto [sock, sockErr] = CreateNetlinkSocket();
    if (!sockErr.IsNone()) {
        return sockErr;
    }

    auto [srcIndex, srcErr] = LookupIfIndex(sock.get(), srcIfName);
    if (!srcErr.IsNone()) {
        return srcErr;
    }

    auto [dstIndex, dstErr] = LookupIfIndex(sock.get(), dstIfName);
    if (!dstErr.IsNone()) {
        return dstErr;
    }

    auto cls = UniqueCls(rtnl_cls_alloc(), rtnl_cls_put);
    if (!cls) {
        return NLToAosErr(errno, "failed to allocate classifier");
    }

    rtnl_tc_set_ifindex(AsTC(cls.get()), srcIndex);
    rtnl_tc_set_parent(AsTC(cls.get()), cIngressParent);

    rtnl_cls_set_prio(cls.get(), cFilterPrio);
    rtnl_cls_set_protocol(cls.get(), ETH_P_ALL);

    if (auto err = rtnl_tc_set_kind(AsTC(cls.get()), "matchall"); err < 0) {
        return NLToAosErr(err, "failed to set classifier kind matchall");
    }

    auto act = UniqueAct(rtnl_act_alloc(), rtnl_act_put);
    if (!act) {
        return NLToAosErr(errno, "failed to allocate action");
    }

    if (auto err = rtnl_tc_set_kind(AsTC(act.get()), "mirred"); err < 0) {
        return NLToAosErr(err, "failed to set action kind mirred");
    }

    if (auto err = rtnl_mirred_set_action(act.get(), TCA_EGRESS_REDIR); err < 0) {
        return NLToAosErr(err, "failed to set mirred action");
    }

    if (auto err = rtnl_mirred_set_policy(act.get(), TC_ACT_STOLEN); err < 0) {
        return NLToAosErr(err, "failed to set mirred policy");
    }

    if (auto err = rtnl_mirred_set_ifindex(act.get(), static_cast<uint32_t>(dstIndex)); err < 0) {
        return NLToAosErr(err, "failed to set mirred ifindex");
    }

    if (auto err = rtnl_mall_append_action(cls.get(), act.get()); err < 0) {
        return NLToAosErr(err, "failed to append mirred action to matchall");
    }

    // Ownership of the action is now transferred to the classifier.
    (void)act.release();

    if (auto err = rtnl_cls_add(sock.get(), cls.get(), NLM_F_CREATE); err < 0) {
        return NLToAosErr(err, "failed to add ingress mirred filter");
    }

    return ErrorEnum::eNone;
}

} // namespace aos::common::network
