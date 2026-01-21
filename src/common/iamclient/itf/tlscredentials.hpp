/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_IAMCLIENT_ITF_TLSCREDENTIALS_HPP_
#define AOS_COMMON_IAMCLIENT_ITF_TLSCREDENTIALS_HPP_

#include <memory>

#include <grpcpp/security/credentials.h>

#include <core/common/tools/error.hpp>

namespace aos::common::iamclient {

/**
 * TLS credentials interface.
 */
class TLSCredentialsItf {
public:
    /**
     * Destructor.
     */
    virtual ~TLSCredentialsItf() = default;

    /**
     * Gets MTLS configuration.
     *
     * @param certStorage Certificate storage.
     * @return MTLS credentials.
     */
    virtual RetWithError<std::shared_ptr<grpc::ChannelCredentials>> GetMTLSClientCredentials(const String& certStorage)
        = 0;

    /**
     * Gets TLS credentials.
     *
     * @return TLS credentials.
     */
    virtual RetWithError<std::shared_ptr<grpc::ChannelCredentials>> GetTLSClientCredentials() = 0;
};

} // namespace aos::common::iamclient

#endif
