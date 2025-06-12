/*
 * Copyright (C) 2024 EPAM Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IAMCLIENT_HPP_
#define IAMCLIENT_HPP_

#include <gmock/gmock.h>

#include "iamclient/publicservicehandler.hpp"

namespace aos::common::iamclient {

using namespace testing;

class TLSCredentialsMock : public aos::common::iamclient::TLSCredentialsItf {
public:
    MOCK_METHOD(aos::RetWithError<std::shared_ptr<grpc::ChannelCredentials>>, GetMTLSClientCredentials,
        (const aos::String&), (override));
    MOCK_METHOD(aos::RetWithError<std::shared_ptr<grpc::ChannelCredentials>>, GetTLSClientCredentials, (), (override));
    MOCK_METHOD(aos::Error, GetCert,
        (const aos::String&, const aos::Array<uint8_t>&, const aos::Array<uint8_t>&, aos::iam::certhandler::CertInfo&),
        (const, override));
    MOCK_METHOD(
        aos::Error, SubscribeCertChanged, (const aos::String&, aos::iam::certhandler::CertReceiverItf&), (override));
    MOCK_METHOD(aos::Error, UnsubscribeCertChanged, (aos::iam::certhandler::CertReceiverItf&), (override));
};

} // namespace aos::common::iamclient

#endif
