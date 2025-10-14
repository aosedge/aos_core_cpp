/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AOS_COMMON_TESTS_MOCKS_IAMCLIENTMOCK_HPP_
#define AOS_COMMON_TESTS_MOCKS_IAMCLIENTMOCK_HPP_

#include <gmock/gmock.h>

#include <common/iamclient/publicservicehandler.hpp>

namespace aos::common::iamclient {

using namespace testing;

class TLSCredentialsMock : public aos::common::iamclient::TLSCredentialsItf {
public:
    MOCK_METHOD(aos::RetWithError<std::shared_ptr<grpc::ChannelCredentials>>, GetMTLSClientCredentials,
        (const aos::String&), (override));
    MOCK_METHOD(aos::RetWithError<std::shared_ptr<grpc::ChannelCredentials>>, GetTLSClientCredentials, (), (override));
    MOCK_METHOD(aos::Error, GetCert,
        (const aos::String&, const aos::Array<uint8_t>&, const aos::Array<uint8_t>&, aos::CertInfo&),
        (const, override));
    MOCK_METHOD(aos::Error, SubscribeListener, (const aos::String&, aos::iamclient::CertListenerItf&), (override));
    MOCK_METHOD(aos::Error, UnsubscribeListener, (aos::iamclient::CertListenerItf&), (override));
};

} // namespace aos::common::iamclient

#endif
