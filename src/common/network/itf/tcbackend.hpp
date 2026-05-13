/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_NETWORK_ITF_TCBACKEND_HPP_
#define AOS_COMMON_NETWORK_ITF_TCBACKEND_HPP_

#include <cstdint>

#include <core/common/tools/error.hpp>
#include <core/common/tools/string.hpp>

namespace aos::common::network {

/**
 * Token-Bucket-Filter qdisc parameters. Rate in bytes/sec, burst and limit
 * in bytes.
 */
struct TBFParams {
    uint64_t mRate {};
    uint64_t mBurst {};
    uint64_t mLimit {};
};

/**
 * Traffic-control backend.
 *
 * Narrow surface over the Linux tc subsystem: TBF qdiscs, ingress qdiscs,
 * and a mirred-action redirect filter. Link lifecycle (IFB device creation
 * and removal) lives in InterfaceFactoryItf / InterfaceManagerItf.
 *
 * All delete operations are best-effort: returning eNone when the target
 * does not exist lets callers treat them as idempotent teardown steps.
 */
class TCBackendItf {
public:
    /**
     * Destructor.
     */
    virtual ~TCBackendItf() = default;

    /**
     * Installs a Token-Bucket-Filter qdisc as the root qdisc of the given
     * interface (handle 1:0, parent TC_H_ROOT).
     *
     * @param ifName interface name.
     * @param params TBF rate/burst/limit.
     * @return Error.
     */
    virtual Error AddRootTBFQDisc(const String& ifName, const TBFParams& params) = 0;

    /**
     * Deletes the root qdisc of the given interface, but only when it is a
     * TBF qdisc. A non-TBF root qdisc (the default pfifo_fast / noqueue, or
     * shaping installed by something else) is left untouched. Returns eNone
     * when there is nothing of ours to remove.
     *
     * @param ifName interface name.
     * @return Error.
     */
    virtual Error DelRootTBFQDisc(const String& ifName) = 0;

    /**
     * Adds an ingress qdisc (handle ffff:0) to the given interface.
     *
     * @param ifName interface name.
     * @return Error.
     */
    virtual Error AddIngressQDisc(const String& ifName) = 0;

    /**
     * Deletes the ingress qdisc of the given interface. Returns eNone when
     * the interface has no ingress qdisc installed.
     *
     * @param ifName interface name.
     * @return Error.
     */
    virtual Error DelIngressQDisc(const String& ifName) = 0;

    /**
     * Installs a matchall classifier on the ingress qdisc of srcIfName whose
     * mirred action redirects every packet to dstIfName's egress.
     *
     * @param srcIfName interface that owns the ingress qdisc.
     * @param dstIfName redirect target (e.g. an IFB pseudo-device).
     * @return Error.
     */
    virtual Error AddIngressMirredFilter(const String& srcIfName, const String& dstIfName) = 0;
};

} // namespace aos::common::network

#endif
