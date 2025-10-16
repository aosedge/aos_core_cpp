/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_IAMCLIENT_ITF_IAMCLIENT_HPP_
#define AOS_COMMON_IAMCLIENT_ITF_IAMCLIENT_HPP_

#include <core/iam/certhandler/certprovider.hpp>
#include <core/iam/nodeinfoprovider/nodeinfoprovider.hpp>
#include <core/iam/permhandler/permhandler.hpp>

namespace aos::sm::iamclient {
/**
 * IAM client interface.
 */
class IAMClientItf : public iam::certhandler::CertProviderItf,
                     public iam::permhandler::PermHandlerItf,
                     public iam::nodeinfoprovider::NodeInfoProviderItf {
public:
    virtual ~IAMClientItf() = default;
};

} // namespace aos::sm::iamclient

#endif // AOS_COMMON_IAMCLIENT_IAMCLIENT_HPP_
