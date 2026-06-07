/*
 * Copyright (C) 2026 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_NETWORK_TC_HPP_
#define AOS_COMMON_NETWORK_TC_HPP_

#include <core/common/tools/error.hpp>
#include <core/common/tools/noncopyable.hpp>

#include "itf/tcbackend.hpp"

namespace aos::common::network {

/**
 * libnl-tc-3 backed TCBackendItf implementation.
 *
 * Stateless: each call opens a short-lived rtnetlink socket. No background
 * threads or long-lived kernel handles.
 */
class TC : public TCBackendItf, private NonCopyable {
public:
    /**
     * Installs a Token-Bucket-Filter qdisc as the root qdisc of an interface.
     *
     * @param ifName interface name.
     * @param params TBF rate/burst/limit.
     * @return error.
     */
    Error AddRootTBFQDisc(const String& ifName, const TBFParams& params) override;

    /**
     * Deletes the root qdisc of an interface, but only when it is a TBF
     * qdisc. A non-TBF root qdisc is left untouched.
     *
     * @param ifName interface name.
     * @return error.
     */
    Error DelRootTBFQDisc(const String& ifName) override;

    /**
     * Adds an ingress qdisc (handle ffff:0) to an interface.
     *
     * @param ifName interface name.
     * @return error.
     */
    Error AddIngressQDisc(const String& ifName) override;

    /**
     * Deletes the ingress qdisc of an interface.
     *
     * @param ifName interface name.
     * @return error.
     */
    Error DelIngressQDisc(const String& ifName) override;

    /**
     * Adds a matchall classifier on the ingress qdisc of srcIfName whose
     * mirred action redirects every packet to dstIfName's egress.
     *
     * @param srcIfName interface that owns the ingress qdisc.
     * @param dstIfName redirect target.
     * @return error.
     */
    Error AddIngressMirredFilter(const String& srcIfName, const String& dstIfName) override;
};

} // namespace aos::common::network

#endif
