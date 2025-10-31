/*
 * Copyright (C) 2025 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_IAMCLIENT_TESTS_MOCKS_TLSCREDENTIALSMOCK_HPP_
#define AOS_COMMON_IAMCLIENT_TESTS_MOCKS_TLSCREDENTIALSMOCK_HPP_

#include <gmock/gmock.h>

#include <grpcpp/security/credentials.h>

#include <common/iamclient/itf/tlscredentials.hpp>

/**
 * Mock for TLSCredentialsItf.
 */
class TLSCredentialsMock : public aos::common::iamclient::TLSCredentialsItf {
public:
    MOCK_METHOD(aos::RetWithError<std::shared_ptr<grpc::ChannelCredentials>>, GetTLSClientCredentials,
        (bool insecureConnection), (override));

    MOCK_METHOD(aos::RetWithError<std::shared_ptr<grpc::ChannelCredentials>>, GetMTLSClientCredentials,
        (const aos::String& certStorage, bool insecureConnection), (override));
};

#endif
